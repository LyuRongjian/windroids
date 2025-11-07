#include "compositor_resource.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>

#define LOG_TAG "ResourceManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 内存池大小等级
#define MEMORY_POOL_SMALL_SIZE   1024      // 1KB
#define MEMORY_POOL_MEDIUM_SIZE  16384     // 16KB
#define MEMORY_POOL_LARGE_SIZE   262144    // 256KB

// 分层内存池结构
struct tiered_memory_pools {
    struct memory_pool* small_pools;   // 小内存池
    struct memory_pool* medium_pools;  // 中等内存池
    struct memory_pool* large_pools;   // 大内存池
    size_t small_pool_count;
    size_t medium_pool_count;
    size_t large_pool_count;
};

// 全局资源管理器状态
static struct resource_manager g_rm = {0};
static struct tiered_memory_pools g_tiered_pools = {0};

// 增量垃圾回收状态
static struct {
    bool in_progress;
    int current_type;
    struct resource* current_resource;
    struct memory_pool* current_pool;
    uint32_t max_objects_per_gc;
    uint64_t last_gc_time;
    uint64_t min_gc_interval;
} g_incremental_gc = {0};

// 异步加载队列
struct async_load_queue {
    struct resource* high_priority_head;  // 高优先级队列头
    struct resource* high_priority_tail;  // 高优先级队列尾
    struct resource* normal_priority_head; // 普通优先级队列头
    struct resource* normal_priority_tail; // 普通优先级队列尾
    uint32_t max_concurrent_loads;        // 最大并发加载数
    uint32_t current_loads;                // 当前加载数
    bool processing;                       // 是否正在处理队列
};

static struct async_load_queue g_async_queue = {0};

// 内部函数声明
static uint64_t resource_get_time(void);
static void resource_update_stats(void);
static int resource_collect_garbage(void);
static int resource_collect_garbage_incremental(void);
static void resource_add_to_async_queue(struct resource* resource);
static void resource_remove_from_async_queue(struct resource* resource);
static int resource_load_in_background(struct resource* resource);

// 初始化资源管理器
int resource_manager_init(size_t memory_limit) {
    if (g_rm.resources[0]) {
        LOGE("Resource manager already initialized");
        return -1;
    }
    
    memset(&g_rm, 0, sizeof(g_rm));
    memset(&g_tiered_pools, 0, sizeof(g_tiered_pools));
    
    g_rm.next_resource_id = 1;
    g_rm.memory_limit = memory_limit;
    g_rm.gc_threshold = memory_limit / 2; // 默认阈值为限制的一半
    g_rm.current_time = resource_get_time();
    g_rm.auto_gc_enabled = true;
    
    // 初始化增量垃圾回收参数
    g_incremental_gc.max_objects_per_gc = 10; // 每次最多处理10个对象
    g_incremental_gc.min_gc_interval = 16666; // 最小间隔16.67ms (60fps)
    g_incremental_gc.last_gc_time = 0;
    g_incremental_gc.in_progress = false;
    g_incremental_gc.current_type = 0;
    g_incremental_gc.current_resource = NULL;
    g_incremental_gc.current_pool = NULL;
    
    // 初始化异步加载队列
    g_async_queue.max_concurrent_loads = 3; // 最多同时加载3个资源
    g_async_queue.current_loads = 0;
    g_async_queue.processing = false;
    g_async_queue.high_priority_head = NULL;
    g_async_queue.high_priority_tail = NULL;
    g_async_queue.normal_priority_head = NULL;
    g_async_queue.normal_priority_tail = NULL;
    
    // 预分配分层内存池
    // 预分配2个小内存池
    for (int i = 0; i < 2; i++) {
        struct memory_pool* pool = resource_create_memory_pool(MEMORY_POOL_SMALL_SIZE);
        if (pool) {
            pool->next = g_tiered_pools.small_pools;
            g_tiered_pools.small_pools = pool;
            g_tiered_pools.small_pool_count++;
        }
    }
    
    // 预分配1个中等内存池
    struct memory_pool* medium_pool = resource_create_memory_pool(MEMORY_POOL_MEDIUM_SIZE);
    if (medium_pool) {
        medium_pool->next = g_tiered_pools.medium_pools;
        g_tiered_pools.medium_pools = medium_pool;
        g_tiered_pools.medium_pool_count++;
    }
    
    LOGI("Resource manager initialized with memory limit %zu bytes", memory_limit);
    LOGI("Pre-allocated tiered memory pools: %zu small, %zu medium, %zu large", 
         g_tiered_pools.small_pool_count, g_tiered_pools.medium_pool_count, g_tiered_pools.large_pool_count);
    return 0;
}

// 销毁资源管理器
void resource_manager_destroy(void) {
    // 销毁所有资源
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        struct resource* resource = g_rm.resources[i];
        while (resource) {
            struct resource* next = resource->next;
            resource_destroy(resource);
            resource = next;
        }
    }
    
    // 销毁所有内存池
    struct memory_pool* pool = g_rm.memory_pools;
    while (pool) {
        struct memory_pool* next = pool->next;
        free(pool->memory);
        free(pool);
        pool = next;
    }
    
    memset(&g_rm, 0, sizeof(g_rm));
    LOGI("Resource manager destroyed");
}

// 创建资源
struct resource* resource_create(resource_type_t type, const char* name, size_t size) {
    if (type < 0 || type >= RESOURCE_TYPE_COUNT) {
        LOGE("Invalid resource type");
        return NULL;
    }
    
    struct resource* resource = (struct resource*)calloc(1, sizeof(struct resource));
    if (!resource) {
        LOGE("Failed to allocate memory for resource");
        return NULL;
    }
    
    resource->id = g_rm.next_resource_id++;
    resource->type = type;
    resource->state = RESOURCE_STATE_UNLOADED;
    resource->size = size;
    resource->async_loading = false;
    resource->high_priority = false;
    resource->load_progress = 0;
    
    if (name) {
        strncpy(resource->name, name, sizeof(resource->name) - 1);
        resource->name[sizeof(resource->name) - 1] = '\0';
    }
    
    // 添加到资源列表
    resource->next = g_rm.resources[type];
    g_rm.resources[type] = resource;
    
    // 更新统计
    g_rm.stats.total_resources++;
    
    LOGI("Created resource %d (type: %d, name: %s, size: %zu)", 
         resource->id, type, resource->name, size);
    
    return resource;
}

// 销毁资源
void resource_destroy(struct resource* resource) {
    if (!resource) return;
    
    // 从资源列表中移除
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        if (g_rm.resources[i] == resource) {
            g_rm.resources[i] = resource->next;
            break;
        }
        
        struct resource* r = g_rm.resources[i];
        while (r && r->next != resource) {
            r = r->next;
        }
        
        if (r) {
            r->next = resource->next;
            break;
        }
    }
    
    // 卸载资源
    if (resource->state == RESOURCE_STATE_LOADED) {
        resource_unload(resource);
    }
    
    // 释放资源数据
    if (resource->data) {
        resource_free(resource->data);
    }
    
    // 更新统计
    g_rm.stats.total_resources--;
    if (resource->state == RESOURCE_STATE_ERROR) {
        g_rm.stats.error_resources--;
    }
    
    LOGI("Destroyed resource %d", resource->id);
    free(resource);
}

// 查找资源
struct resource* resource_find(uint32_t id) {
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        struct resource* resource = g_rm.resources[i];
        while (resource) {
            if (resource->id == id) {
                return resource;
            }
            resource = resource->next;
        }
    }
    
    return NULL;
}

// 按名称查找资源
struct resource* resource_find_by_name(const char* name) {
    if (!name) return NULL;
    
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        struct resource* resource = g_rm.resources[i];
        while (resource) {
            if (strcmp(resource->name, name) == 0) {
                return resource;
            }
            resource = resource->next;
        }
    }
    
    return NULL;
}

// 加载资源
int resource_load(struct resource* resource) {
    if (!resource) {
        LOGE("Invalid resource");
        return -1;
    }
    
    if (resource->state == RESOURCE_STATE_LOADED) {
        return 0;
    }
    
    if (resource->state == RESOURCE_STATE_LOADING) {
        LOGE("Resource %d already loading", resource->id);
        return -1;
    }
    
    resource->state = RESOURCE_STATE_LOADING;
    
    // 分配内存
    if (!resource->data && resource->size > 0) {
        resource->data = resource_allocate(resource->size);
        if (!resource->data) {
            LOGE("Failed to allocate memory for resource %d", resource->id);
            resource->state = RESOURCE_STATE_ERROR;
            g_rm.stats.error_resources++;
            return -1;
        }
    }
    
    // 实际实现中应根据资源类型加载资源
    // 这里只是模拟加载过程
    memset(resource->data, 0, resource->size);
    
    resource->state = RESOURCE_STATE_LOADED;
    g_rm.stats.loaded_resources++;
    
    LOGI("Loaded resource %d", resource->id);
    return 0;
}

// 卸载资源
void resource_unload(struct resource* resource) {
    if (!resource || resource->state != RESOURCE_STATE_LOADED) {
        return;
    }
    
    // 释放资源数据
    if (resource->data) {
        resource_free(resource->data);
        resource->data = NULL;
    }
    
    resource->state = RESOURCE_STATE_UNLOADED;
    g_rm.stats.loaded_resources--;
    
    LOGI("Unloaded resource %d", resource->id);
}

// 增加资源引用
void resource_add_ref(struct resource* resource) {
    if (!resource) return;
    
    resource->usage.ref_count++;
    resource_update_usage(resource);
}

// 减少资源引用
void resource_release(struct resource* resource) {
    if (!resource) return;
    
    if (resource->usage.ref_count > 0) {
        resource->usage.ref_count--;
    }
    
    resource_update_usage(resource);
    
    // 如果引用计数为0且自动垃圾回收启用，触发垃圾回收
    if (resource->usage.ref_count == 0 && g_rm.auto_gc_enabled) {
        resource_gc();
    }
}

// 更新资源使用统计
void resource_update_usage(struct resource* resource) {
    if (!resource) return;
    
    uint64_t current_time = resource_get_time();
    uint64_t time_diff = current_time - resource->usage.last_used;
    
    resource->usage.last_used = current_time;
    resource->usage.use_count++;
    resource->usage.total_time += time_diff;
}

// 分配内存
void* resource_allocate(size_t size) {
    // 检查内存限制
    if (g_rm.stats.total_memory + size > g_rm.memory_limit) {
        LOGE("Memory limit exceeded: %zu + %zu > %zu", 
             g_rm.stats.total_memory, size, g_rm.memory_limit);
        
        // 尝试垃圾回收
        if (resource_gc() != 0) {
            return NULL;
        }
        
        // 再次检查
        if (g_rm.stats.total_memory + size > g_rm.memory_limit) {
            LOGE("Memory limit still exceeded after GC");
            return NULL;
        }
    }
    
    struct memory_pool* pool = NULL;
    
    // 根据大小选择合适的内存池
    if (size <= MEMORY_POOL_SMALL_SIZE) {
        // 尝试从小内存池分配
        pool = g_tiered_pools.small_pools;
        while (pool) {
            if (pool->size - pool->used >= size) {
                void* ptr = (char*)pool->memory + pool->used;
                pool->used += size;
                g_rm.stats.total_memory += size;
                
                // 更新峰值内存使用
                if (g_rm.stats.total_memory > g_rm.stats.peak_memory) {
                    g_rm.stats.peak_memory = g_rm.stats.total_memory;
                }
                
                return ptr;
            }
            pool = pool->next;
        }
        
        // 如果没有合适的小内存池，创建一个新的
        pool = resource_create_memory_pool(MEMORY_POOL_SMALL_SIZE);
        if (pool) {
            pool->next = g_tiered_pools.small_pools;
            g_tiered_pools.small_pools = pool;
            g_tiered_pools.small_pool_count++;
            
            void* ptr = pool->memory;
            pool->used = size;
            g_rm.stats.total_memory += size;
            
            // 更新峰值内存使用
            if (g_rm.stats.total_memory > g_rm.stats.peak_memory) {
                g_rm.stats.peak_memory = g_rm.stats.total_memory;
            }
            
            return ptr;
        }
    } else if (size <= MEMORY_POOL_MEDIUM_SIZE) {
        // 尝试从中等内存池分配
        pool = g_tiered_pools.medium_pools;
        while (pool) {
            if (pool->size - pool->used >= size) {
                void* ptr = (char*)pool->memory + pool->used;
                pool->used += size;
                g_rm.stats.total_memory += size;
                
                // 更新峰值内存使用
                if (g_rm.stats.total_memory > g_rm.stats.peak_memory) {
                    g_rm.stats.peak_memory = g_rm.stats.total_memory;
                }
                
                return ptr;
            }
            pool = pool->next;
        }
        
        // 如果没有合适的中等内存池，创建一个新的
        pool = resource_create_memory_pool(MEMORY_POOL_MEDIUM_SIZE);
        if (pool) {
            pool->next = g_tiered_pools.medium_pools;
            g_tiered_pools.medium_pools = pool;
            g_tiered_pools.medium_pool_count++;
            
            void* ptr = pool->memory;
            pool->used = size;
            g_rm.stats.total_memory += size;
            
            // 更新峰值内存使用
            if (g_rm.stats.total_memory > g_rm.stats.peak_memory) {
                g_rm.stats.peak_memory = g_rm.stats.total_memory;
            }
            
            return ptr;
        }
    } else {
        // 尝试从大内存池分配
        pool = g_tiered_pools.large_pools;
        while (pool) {
            if (pool->size - pool->used >= size) {
                void* ptr = (char*)pool->memory + pool->used;
                pool->used += size;
                g_rm.stats.total_memory += size;
                
                // 更新峰值内存使用
                if (g_rm.stats.total_memory > g_rm.stats.peak_memory) {
                    g_rm.stats.peak_memory = g_rm.stats.total_memory;
                }
                
                return ptr;
            }
            pool = pool->next;
        }
        
        // 如果没有合适的大内存池，创建一个新的
        size_t pool_size = size * 2; // 分配比请求更大的池
        if (pool_size < MEMORY_POOL_LARGE_SIZE) pool_size = MEMORY_POOL_LARGE_SIZE;
        
        pool = resource_create_memory_pool(pool_size);
        if (pool) {
            pool->next = g_tiered_pools.large_pools;
            g_tiered_pools.large_pools = pool;
            g_tiered_pools.large_pool_count++;
            
            void* ptr = pool->memory;
            pool->used = size;
            g_rm.stats.total_memory += size;
            
            // 更新峰值内存使用
            if (g_rm.stats.total_memory > g_rm.stats.peak_memory) {
                g_rm.stats.peak_memory = g_rm.stats.total_memory;
            }
            
            return ptr;
        }
    }
    
    // 如果所有分层内存池都失败，回退到原来的方法
    pool = g_rm.memory_pools;
    while (pool) {
        if (pool->size - pool->used >= size) {
            void* ptr = (char*)pool->memory + pool->used;
            pool->used += size;
            g_rm.stats.total_memory += size;
            
            // 更新峰值内存使用
            if (g_rm.stats.total_memory > g_rm.stats.peak_memory) {
                g_rm.stats.peak_memory = g_rm.stats.total_memory;
            }
            
            return ptr;
        }
        pool = pool->next;
    }
    
    // 创建新的内存池
    size_t pool_size = size * 2; // 分配比请求更大的池
    if (pool_size < 1024) pool_size = 1024; // 最小1KB
    
    struct memory_pool* new_pool = resource_create_memory_pool(pool_size);
    if (!new_pool) {
        LOGE("Failed to create memory pool");
        return NULL;
    }
    
    // 从新池分配
    void* ptr = new_pool->memory;
    new_pool->used = size;
    g_rm.stats.total_memory += size;
    
    // 更新峰值内存使用
    if (g_rm.stats.total_memory > g_rm.stats.peak_memory) {
        g_rm.stats.peak_memory = g_rm.stats.total_memory;
    }
    
    return ptr;
}

// 释放内存
void resource_free(void* ptr) {
    if (!ptr) return;
    
    // 简化实现：不实际释放内存，由垃圾回收处理
    // 实际实现中应更精确地跟踪内存分配和释放
}

// 创建内存池
struct memory_pool* resource_create_memory_pool(size_t size) {
    struct memory_pool* pool = (struct memory_pool*)malloc(sizeof(struct memory_pool));
    if (!pool) {
        LOGE("Failed to allocate memory for memory pool");
        return NULL;
    }
    
    pool->memory = malloc(size);
    if (!pool->memory) {
        LOGE("Failed to allocate memory pool");
        free(pool);
        return NULL;
    }
    
    pool->size = size;
    pool->used = 0;
    pool->next = g_rm.memory_pools;
    g_rm.memory_pools = pool;
    
    g_rm.stats.memory_pools++;
    
    LOGI("Created memory pool with size %zu", size);
    return pool;
}

// 销毁内存池
void resource_destroy_memory_pool(struct memory_pool* pool) {
    if (!pool) return;
    
    // 从内存池列表中移除
    if (g_rm.memory_pools == pool) {
        g_rm.memory_pools = pool->next;
    } else {
        struct memory_pool* p = g_rm.memory_pools;
        while (p && p->next != pool) {
            p = p->next;
        }
        if (p) {
            p->next = pool->next;
        }
    }
    
    // 更新统计
    g_rm.stats.total_memory -= pool->used;
    g_rm.stats.memory_pools--;
    
    free(pool->memory);
    free(pool);
}

// 执行垃圾回收
int resource_gc(void) {
    uint64_t current_time = resource_get_time();
    
    // 检查是否需要等待
    if (g_incremental_gc.last_gc_time > 0 && 
        current_time - g_incremental_gc.last_gc_time < g_incremental_gc.min_gc_interval) {
        return 0; // 跳过本次回收
    }
    
    uint64_t start_time = current_time;
    
    // 如果没有正在进行的增量回收，开始新的回收
    if (!g_incremental_gc.in_progress) {
        g_incremental_gc.in_progress = true;
        g_incremental_gc.current_type = 0;
        g_incremental_gc.current_resource = g_rm.resources[0];
        g_incremental_gc.current_pool = g_tiered_pools.small_pools;
    }
    
    int result = resource_collect_garbage_incremental();
    
    uint64_t end_time = resource_get_time();
    g_rm.stats.gc_time += end_time - start_time;
    g_rm.stats.gc_count++;
    g_incremental_gc.last_gc_time = end_time;
    
    // 如果回收完成，重置状态
    if (result == 1) {
        g_incremental_gc.in_progress = false;
        LOGI("Incremental garbage collection completed in %llu ms", (end_time - start_time) / 1000);
    }
    
    return 0;
}

// 设置内存限制
void resource_set_memory_limit(size_t limit) {
    g_rm.memory_limit = limit;
}

// 设置垃圾回收阈值
void resource_set_gc_threshold(size_t threshold) {
    g_rm.gc_threshold = threshold;
}

// 启用/禁用自动垃圾回收
void resource_set_auto_gc_enabled(bool enabled) {
    g_rm.auto_gc_enabled = enabled;
}

// 获取资源管理器统计
void resource_get_stats(struct resource_manager_stats* stats) {
    if (!stats) return;
    
    *stats = g_rm.stats;
}

// 重置资源管理器统计
void resource_reset_stats(void) {
    memset(&g_rm.stats, 0, sizeof(g_rm.stats));
}

// 更新资源管理器（每帧调用）
void resource_manager_update(void) {
    // 更新当前时间
    g_rm.current_time = resource_get_time();
    
    // 检查是否需要自动垃圾回收
    if (g_rm.auto_gc_enabled && 
        g_rm.stats.total_memory > g_rm.gc_threshold) {
        resource_gc();
    }
    
    // 更新统计
    resource_update_stats();
}

// 打印资源使用情况
void resource_print_usage(void) {
    LOGI("=== Resource Usage ===");
    LOGI("Total resources: %u", g_rm.stats.total_resources);
    LOGI("Loaded resources: %u", g_rm.stats.loaded_resources);
    LOGI("Error resources: %u", g_rm.stats.error_resources);
    LOGI("Total memory: %zu bytes", g_rm.stats.total_memory);
    LOGI("Peak memory: %zu bytes", g_rm.stats.peak_memory);
    LOGI("Memory pools: %u", g_rm.stats.memory_pools);
    LOGI("GC count: %u", g_rm.stats.gc_count);
    LOGI("GC time: %llu ms", g_rm.stats.gc_time / 1000);
    
    // 打印每种类型的资源数量
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        uint32_t count = 0;
        struct resource* resource = g_rm.resources[i];
        while (resource) {
            count++;
            resource = resource->next;
        }
        LOGI("Type %d resources: %u", i, count);
    }
}

// 内部函数：获取当前时间（微秒）
static uint64_t resource_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

// 内部函数：更新统计
static void resource_update_stats(void) {
    // 更新已加载资源数
    uint32_t loaded = 0;
    uint32_t error = 0;
    
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        struct resource* resource = g_rm.resources[i];
        while (resource) {
            if (resource->state == RESOURCE_STATE_LOADED) {
                loaded++;
            } else if (resource->state == RESOURCE_STATE_ERROR) {
                error++;
            }
            resource = resource->next;
        }
    }
    
    g_rm.stats.loaded_resources = loaded;
    g_rm.stats.error_resources = error;
}

// 内部函数：执行垃圾回收
static int resource_collect_garbage(void) {
    uint32_t freed_count = 0;
    size_t freed_memory = 0;
    
    // 收集未使用的资源
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        struct resource* resource = g_rm.resources[i];
        while (resource) {
            struct resource* next = resource->next;
            
            // 如果引用计数为0且资源已加载，卸载资源
            if (resource->usage.ref_count == 0 && 
                resource->state == RESOURCE_STATE_LOADED) {
                size_t size = resource->size;
                resource_unload(resource);
                freed_memory += size;
                freed_count++;
            }
            
            resource = next;
        }
    }
    
    // 收集未使用的内存池
    struct memory_pool* pool = g_rm.memory_pools;
    while (pool) {
        struct memory_pool* next = pool->next;
        
        // 如果内存池未使用，销毁它
        if (pool->used == 0) {
            resource_destroy_memory_pool(pool);
        }
        
        pool = next;
    }
    
    LOGI("Garbage collection freed %u resources and %zu bytes", freed_count, freed_memory);
    
    return 0;
}

// 增量垃圾回收实现
static int resource_collect_garbage_incremental(void) {
    uint32_t freed_count = 0;
    size_t freed_memory = 0;
    uint32_t objects_processed = 0;
    
    // 处理资源
    while (g_incremental_gc.current_type < RESOURCE_TYPE_COUNT && 
           objects_processed < g_incremental_gc.max_objects_per_gc) {
        
        // 如果当前资源为空，移动到下一个类型的第一个资源
        if (!g_incremental_gc.current_resource) {
            g_incremental_gc.current_type++;
            if (g_incremental_gc.current_type < RESOURCE_TYPE_COUNT) {
                g_incremental_gc.current_resource = g_rm.resources[g_incremental_gc.current_type];
            }
            continue;
        }
        
        // 检查当前资源
        struct resource* next = g_incremental_gc.current_resource->next;
        
        // 如果引用计数为0且资源已加载，卸载资源
        if (g_incremental_gc.current_resource->usage.ref_count == 0 && 
            g_incremental_gc.current_resource->state == RESOURCE_STATE_LOADED) {
            size_t size = g_incremental_gc.current_resource->size;
            resource_unload(g_incremental_gc.current_resource);
            freed_memory += size;
            freed_count++;
        }
        
        g_incremental_gc.current_resource = next;
        objects_processed++;
    }
    
    // 如果资源处理完成，开始处理内存池
    if (g_incremental_gc.current_type >= RESOURCE_TYPE_COUNT && 
        objects_processed < g_incremental_gc.max_objects_per_gc) {
        
        // 处理小内存池
        while (g_incremental_gc.current_pool && 
               objects_processed < g_incremental_gc.max_objects_per_gc) {
            
            struct memory_pool* next = g_incremental_gc.current_pool->next;
            
            // 如果内存池未使用，销毁它
            if (g_incremental_gc.current_pool->used == 0) {
                // 从分层内存池中移除
                if (g_incremental_gc.current_pool == g_tiered_pools.small_pools) {
                    g_tiered_pools.small_pools = next;
                    g_tiered_pools.small_pool_count--;
                } else if (g_incremental_gc.current_pool == g_tiered_pools.medium_pools) {
                    g_tiered_pools.medium_pools = next;
                    g_tiered_pools.medium_pool_count--;
                } else if (g_incremental_gc.current_pool == g_tiered_pools.large_pools) {
                    g_tiered_pools.large_pools = next;
                    g_tiered_pools.large_pool_count--;
                } else {
                    // 在普通内存池列表中查找并移除
                    struct memory_pool* prev = g_rm.memory_pools;
                    if (prev == g_incremental_gc.current_pool) {
                        g_rm.memory_pools = next;
                    } else {
                        while (prev && prev->next != g_incremental_gc.current_pool) {
                            prev = prev->next;
                        }
                        if (prev) {
                            prev->next = next;
                        }
                    }
                }
                
                // 更新统计
                g_rm.stats.total_memory -= g_incremental_gc.current_pool->used;
                g_rm.stats.memory_pools--;
                
                free(g_incremental_gc.current_pool->memory);
                free(g_incremental_gc.current_pool);
            }
            
            g_incremental_gc.current_pool = next;
            objects_processed++;
        }
        
        // 如果小内存池处理完成，移动到中等内存池
        if (!g_incremental_gc.current_pool && 
            g_incremental_gc.current_pool == g_tiered_pools.small_pools) {
            g_incremental_gc.current_pool = g_tiered_pools.medium_pools;
        }
        // 如果中等内存池处理完成，移动到大内存池
        else if (!g_incremental_gc.current_pool && 
                 g_incremental_gc.current_pool == g_tiered_pools.medium_pools) {
            g_incremental_gc.current_pool = g_tiered_pools.large_pools;
        }
        // 如果大内存池处理完成，移动到普通内存池
        else if (!g_incremental_gc.current_pool && 
                 g_incremental_gc.current_pool == g_tiered_pools.large_pools) {
            g_incremental_gc.current_pool = g_rm.memory_pools;
        }
    }
    
    // 检查是否所有处理完成
    if (g_incremental_gc.current_type >= RESOURCE_TYPE_COUNT && 
        !g_incremental_gc.current_pool) {
        LOGI("Incremental garbage collection freed %u resources and %zu bytes", freed_count, freed_memory);
        return 1; // 完成
    }
    
    return 0; // 未完成，下次继续
}

// 异步加载队列节点
struct async_load_node {
    struct resource* resource;
    struct async_load_node* next;
};

// 内部函数：将资源添加到异步加载队列
static void resource_add_to_async_queue(struct resource* resource) {
    struct async_load_node* node = malloc(sizeof(struct async_load_node));
    if (!node) {
        LOGE("Failed to allocate async load node");
        return;
    }
    
    node->resource = resource;
    node->next = NULL;
    
    // 根据优先级添加到队列
    if (resource->high_priority) {
        // 高优先级队列
        if (g_async_queue.high_priority_tail) {
            g_async_queue.high_priority_tail->next = node;
            g_async_queue.high_priority_tail = node;
        } else {
            g_async_queue.high_priority_head = node;
            g_async_queue.high_priority_tail = node;
        }
    } else {
        // 普通优先级队列
        if (g_async_queue.normal_priority_tail) {
            g_async_queue.normal_priority_tail->next = node;
            g_async_queue.normal_priority_tail = node;
        } else {
            g_async_queue.normal_priority_head = node;
            g_async_queue.normal_priority_tail = node;
        }
    }
    
    // 设置资源状态
    resource->async_loading = true;
    resource->state = RESOURCE_STATE_LOADING;
    resource->load_progress = 0;
}

// 内部函数：从异步加载队列中移除资源
static void resource_remove_from_async_queue(struct resource* resource) {
    struct async_load_node* prev = NULL;
    struct async_load_node* current = NULL;
    
    // 检查高优先级队列
    current = g_async_queue.high_priority_head;
    while (current) {
        if (current->resource == resource) {
            // 从队列中移除
            if (prev) {
                prev->next = current->next;
            } else {
                g_async_queue.high_priority_head = current->next;
            }
            
            // 更新尾指针
            if (current == g_async_queue.high_priority_tail) {
                g_async_queue.high_priority_tail = prev;
            }
            
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
    
    // 检查普通优先级队列
    prev = NULL;
    current = g_async_queue.normal_priority_head;
    while (current) {
        if (current->resource == resource) {
            // 从队列中移除
            if (prev) {
                prev->next = current->next;
            } else {
                g_async_queue.normal_priority_head = current->next;
            }
            
            // 更新尾指针
            if (current == g_async_queue.normal_priority_tail) {
                g_async_queue.normal_priority_tail = prev;
            }
            
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

// 内部函数：在后台加载资源
static int resource_load_in_background(struct resource* resource) {
    // 模拟加载过程
    // 在实际实现中，这里会执行真正的资源加载
    // 这里我们只是模拟进度更新
    
    uint32_t total_steps = 10;
    for (uint32_t i = 0; i < total_steps; i++) {
        // 检查是否已被取消
        if (!resource->async_loading) {
            return -1;
        }
        
        // 更新进度
        resource->load_progress = (i + 1) * 100 / total_steps;
        
        // 模拟加载延迟
        usleep(10000); // 10ms
    }
    
    // 完成加载
    resource->state = RESOURCE_STATE_LOADED;
    resource->async_loading = false;
    resource->load_progress = 100;
    
    return 0;
}

// 异步加载资源
int resource_load_async(struct resource* resource, bool high_priority) {
    if (!resource) {
        return -1;
    }
    
    // 如果资源已经在加载或已加载，返回错误
    if (resource->state == RESOURCE_STATE_LOADING || 
        resource->state == RESOURCE_STATE_LOADED) {
        return -1;
    }
    
    // 设置高优先级
    resource->high_priority = high_priority;
    
    // 添加到异步加载队列
    resource_add_to_async_queue(resource);
    
    return 0;
}

// 获取资源加载进度
uint32_t resource_get_load_progress(struct resource* resource) {
    if (!resource) {
        return 0;
    }
    
    return resource->load_progress;
}

// 取消异步加载
void resource_cancel_async_load(struct resource* resource) {
    if (!resource || !resource->async_loading) {
        return;
    }
    
    // 从队列中移除
    resource_remove_from_async_queue(resource);
    
    // 重置状态
    resource->async_loading = false;
    resource->state = RESOURCE_STATE_UNLOADED;
    resource->load_progress = 0;
}

// 处理异步加载队列
void resource_process_async_loads(void) {
    // 如果已经在处理中，直接返回
    if (g_async_queue.processing) {
        return;
    }
    
    g_async_queue.processing = true;
    
    // 处理高优先级队列
    while (g_async_queue.current_loads < g_async_queue.max_concurrent_loads && 
           g_async_queue.high_priority_head) {
        struct async_load_node* node = g_async_queue.high_priority_head;
        g_async_queue.high_priority_head = node->next;
        
        if (!g_async_queue.high_priority_head) {
            g_async_queue.high_priority_tail = NULL;
        }
        
        // 启动后台加载
        // 在实际实现中，这里可能会启动线程或任务
        // 这里我们直接调用加载函数
        g_async_queue.current_loads++;
        resource_load_in_background(node->resource);
        g_async_queue.current_loads--;
        
        free(node);
    }
    
    // 处理普通优先级队列
    while (g_async_queue.current_loads < g_async_queue.max_concurrent_loads && 
           g_async_queue.normal_priority_head) {
        struct async_load_node* node = g_async_queue.normal_priority_head;
        g_async_queue.normal_priority_head = node->next;
        
        if (!g_async_queue.normal_priority_head) {
            g_async_queue.normal_priority_tail = NULL;
        }
        
        // 启动后台加载
        g_async_queue.current_loads++;
        resource_load_in_background(node->resource);
        g_async_queue.current_loads--;
        
        free(node);
    }
    
    g_async_queue.processing = false;
}

// 更新资源管理器（每帧调用）
void resource_manager_update(void) {
    // 更新当前时间
    g_resource_manager.current_time = get_current_time_ms();
    
    // 处理异步加载队列
    resource_process_async_loads();
    
    // 如果启用了自动垃圾回收，检查是否需要执行垃圾回收
    if (g_resource_manager.auto_gc_enabled) {
        size_t memory_usage = g_resource_manager.stats.total_memory;
        
        // 如果内存使用超过阈值，执行垃圾回收
        if (memory_usage > g_resource_manager.gc_threshold) {
            // 使用增量垃圾回收，避免长时间阻塞
            resource_collect_garbage_incremental();
        }
    }
}