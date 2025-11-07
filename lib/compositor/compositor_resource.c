#include "compositor_resource.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>

#define LOG_TAG "ResourceManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 全局资源管理器状态
static struct resource_manager g_rm = {0};

// 内部函数声明
static uint64_t resource_get_time(void);
static void resource_update_stats(void);
static int resource_collect_garbage(void);

// 初始化资源管理器
int resource_manager_init(size_t memory_limit) {
    if (g_rm.resources[0]) {
        LOGE("Resource manager already initialized");
        return -1;
    }
    
    memset(&g_rm, 0, sizeof(g_rm));
    g_rm.next_resource_id = 1;
    g_rm.memory_limit = memory_limit;
    g_rm.gc_threshold = memory_limit / 2; // 默认阈值为限制的一半
    g_rm.current_time = resource_get_time();
    g_rm.auto_gc_enabled = true;
    
    LOGI("Resource manager initialized with memory limit %zu bytes", memory_limit);
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
    
    // 尝试从内存池分配
    struct memory_pool* pool = g_rm.memory_pools;
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
    uint64_t start_time = resource_get_time();
    
    int result = resource_collect_garbage();
    
    uint64_t end_time = resource_get_time();
    g_rm.stats.gc_time += end_time - start_time;
    g_rm.stats.gc_count++;
    
    LOGI("Garbage collection completed in %llu ms", (end_time - start_time) / 1000);
    
    return result;
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