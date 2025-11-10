#include "compositor_memory_pool_opt.h"
#include "compositor_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <stdatomic.h>

// 内存块魔数
#define MEMORY_POOL_MAGIC 0xDEADBEEF

// 默认配置
#define DEFAULT_INITIAL_BLOCK_COUNT 16
#define DEFAULT_MAX_BLOCK_COUNT 1024
#define DEFAULT_GROWTH_FACTOR 150  // 150%
#define DEFAULT_SHRINK_THRESHOLD 50  // 50%
#define DEFAULT_AUTO_RESIZE_INTERVAL_MS 5000  // 5秒
#define DEFAULT_MAX_CACHE_COUNT 64

// 线程本地缓存
static __thread struct memory_pool_thread_cache g_thread_cache = {0};

// 内存池管理器
static struct memory_pool_opt_manager g_pool_manager = {0};

// 默认内存池ID
static uint32_t g_default_pool_id = 0;

// 原子计数器
static atomic_uint g_next_pool_id = ATOMIC_VAR_INIT(1);

// 获取当前时间(纳秒)
static inline uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// 获取块大小等级
static inline uint32_t get_size_class(size_t size)
{
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        if (size <= g_memory_pool_block_sizes[i]) {
            return i;
        }
    }
    return MEMORY_POOL_BLOCK_SIZE_COUNT - 1; // 最大等级
}

// 获取块大小
static inline size_t get_block_size(uint32_t size_class)
{
    if (size_class < MEMORY_POOL_BLOCK_SIZE_COUNT) {
        return g_memory_pool_block_sizes[size_class];
    }
    return 0;
}

// 计算对齐后的大小
static inline size_t align_size(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

// 初始化块头部
static inline void init_block_header(struct memory_pool_block_header* header, 
                                    uint32_t size_class, uint32_t pool_id)
{
    header->magic = MEMORY_POOL_MAGIC;
    header->size_class = size_class;
    header->alloc_time = get_time_ns();
    header->pool_id = pool_id;
}

// 验证块头部
static inline bool validate_block_header(struct memory_pool_block_header* header)
{
    return header && header->magic == MEMORY_POOL_MAGIC;
}

// 获取块头部
static inline struct memory_pool_block_header* get_block_header(void* ptr)
{
    if (!ptr) return NULL;
    return (struct memory_pool_block_header*)((char*)ptr - sizeof(struct memory_pool_block_header));
}

// 获取块数据指针
static inline void* get_block_data(struct memory_pool_block_header* header)
{
    if (!header) return NULL;
    return (char*)header + sizeof(struct memory_pool_block_header);
}

// 初始化线程本地缓存
static int init_thread_cache(void)
{
    if (g_thread_cache.initialized) {
        return 0;
    }
    
    memset(&g_thread_cache, 0, sizeof(g_thread_cache));
    g_thread_cache.thread_id = (uint32_t)gettid();
    g_thread_cache.last_flush_time = get_time_ns();
    
    // 设置最大缓存数量
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        g_thread_cache.max_cache_counts[i] = DEFAULT_MAX_CACHE_COUNT;
    }
    
    g_thread_cache.initialized = true;
    return 0;
}

// 刷新线程本地缓存
static void flush_thread_cache(void)
{
    if (!g_thread_cache.initialized) {
        return;
    }
    
    struct memory_pool_opt* pool = memory_pool_opt_get_default();
    if (!pool) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        struct memory_pool_block_header* block = g_thread_cache.free_lists[i];
        while (block) {
            struct memory_pool_block_header* next = block->next;
            
            // 验证块头部
            if (validate_block_header(block)) {
                // 添加到池的空闲列表
                block->next = pool->free_lists[i];
                pool->free_lists[i] = block;
                pool->used_block_counts[i]--;
            } else {
                // 块损坏，记录错误
                compositor_log_error("Corrupted block in thread cache");
            }
            
            block = next;
        }
        
        g_thread_cache.free_lists[i] = NULL;
        g_thread_cache.cache_counts[i] = 0;
    }
    
    pthread_mutex_unlock(&pool->mutex);
    
    g_thread_cache.last_flush_time = get_time_ns();
}

// 扩展内存池
static int expand_pool(struct memory_pool_opt* pool, uint32_t size_class)
{
    if (!pool || size_class >= MEMORY_POOL_BLOCK_SIZE_COUNT) {
        return -1;
    }
    
    size_t block_size = get_block_size(size_class);
    if (!block_size) {
        return -1;
    }
    
    // 检查是否已达到最大块数
    if (pool->block_counts[size_class] >= pool->max_block_counts[size_class]) {
        return -1;
    }
    
    // 计算新块数量
    uint32_t current_count = pool->block_counts[size_class];
    uint32_t new_count = current_count * pool->config.growth_factor / 100;
    
    // 确保至少增加一个块
    if (new_count <= current_count) {
        new_count = current_count + 1;
    }
    
    // 限制最大块数
    if (new_count > pool->max_block_counts[size_class]) {
        new_count = pool->max_block_counts[size_class];
    }
    
    uint32_t blocks_to_add = new_count - current_count;
    size_t total_size = blocks_to_add * (block_size + sizeof(struct memory_pool_block_header));
    
    // 分配新内存
    void* new_memory = malloc(total_size);
    if (!new_memory) {
        return -1;
    }
    
    // 初始化新块
    char* block_ptr = (char*)new_memory;
    for (uint32_t i = 0; i < blocks_to_add; i++) {
        struct memory_pool_block_header* header = (struct memory_pool_block_header*)block_ptr;
        init_block_header(header, size_class, pool->pool_id);
        
        // 添加到空闲列表
        header->next = pool->free_lists[size_class];
        pool->free_lists[size_class] = header;
        
        block_ptr += block_size + sizeof(struct memory_pool_block_header);
    }
    
    // 更新统计
    pool->block_counts[size_class] = new_count;
    pool->total_size += total_size;
    pool->stats.pool_expansions++;
    
    return 0;
}

// 收缩内存池
static int shrink_pool(struct memory_pool_opt* pool, uint32_t size_class)
{
    if (!pool || size_class >= MEMORY_POOL_BLOCK_SIZE_COUNT) {
        return -1;
    }
    
    // 计算使用率
    uint32_t used_count = pool->used_block_counts[size_class];
    uint32_t total_count = pool->block_counts[size_class];
    
    if (total_count == 0) {
        return 0;
    }
    
    uint32_t utilization = used_count * 100 / total_count;
    
    // 如果使用率高于阈值，不收缩
    if (utilization > pool->config.shrink_threshold) {
        return 0;
    }
    
    // 计算要释放的块数量
    uint32_t free_count = total_count - used_count;
    uint32_t blocks_to_free = free_count / 2;  // 释放一半空闲块
    
    if (blocks_to_free == 0) {
        return 0;
    }
    
    size_t block_size = get_block_size(size_class);
    if (!block_size) {
        return -1;
    }
    
    // 收集要释放的块
    struct memory_pool_block_header** prev = &pool->free_lists[size_class];
    uint32_t freed_count = 0;
    
    while (*prev && freed_count < blocks_to_free) {
        struct memory_pool_block_header* block = *prev;
        
        // 验证块头部
        if (validate_block_header(block)) {
            // 从空闲列表中移除
            *prev = block->next;
            freed_count++;
        } else {
            // 块损坏，跳过
            prev = &block->next;
        }
    }
    
    if (freed_count == 0) {
        return 0;
    }
    
    // 更新统计
    pool->block_counts[size_class] -= freed_count;
    size_t freed_size = freed_count * (block_size + sizeof(struct memory_pool_block_header));
    pool->total_size -= freed_size;
    pool->stats.pool_shrinks++;
    
    return 0;
}

// 自动调整内存池大小
static void auto_resize_pool(struct memory_pool_opt* pool)
{
    if (!pool || !pool->initialized) {
        return;
    }
    
    uint64_t current_time = get_time_ns();
    
    // 检查是否到了调整时间
    if (current_time - pool->last_resize_time < 
        pool->config.auto_resize_interval_ms * 1000000ULL) {
        return;
    }
    
    pool->last_resize_time = current_time;
    
    // 对每个大小等级进行调整
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        // 检查是否需要扩展
        if (pool->used_block_counts[i] >= pool->block_counts[i] * 0.8) {
            expand_pool(pool, i);
        }
        // 检查是否需要收缩
        else if (pool->used_block_counts[i] <= pool->block_counts[i] * 0.2) {
            shrink_pool(pool, i);
        }
    }
}

// ==================== 内存池管理API实现 ====================

int memory_pool_opt_manager_init(uint32_t max_pools)
{
    if (g_pool_manager.initialized) {
        return 0;
    }
    
    memset(&g_pool_manager, 0, sizeof(g_pool_manager));
    
    // 分配内存池数组
    g_pool_manager.pools = calloc(max_pools, sizeof(struct memory_pool_opt*));
    if (!g_pool_manager.pools) {
        return -1;
    }
    
    g_pool_manager.max_pools = max_pools;
    g_pool_manager.pool_count = 0;
    
    // 初始化默认配置
    memset(&g_pool_manager.default_config, 0, sizeof(g_pool_manager.default_config));
    
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        g_pool_manager.default_config.initial_block_counts[i] = DEFAULT_INITIAL_BLOCK_COUNT;
        g_pool_manager.default_config.max_block_counts[i] = DEFAULT_MAX_BLOCK_COUNT;
    }
    
    g_pool_manager.default_config.growth_factor = DEFAULT_GROWTH_FACTOR;
    g_pool_manager.default_config.shrink_threshold = DEFAULT_SHRINK_THRESHOLD;
    g_pool_manager.default_config.auto_resize_interval_ms = DEFAULT_AUTO_RESIZE_INTERVAL_MS;
    g_pool_manager.default_config.enable_thread_cache = true;
    g_pool_manager.default_config.enable_prefetch = true;
    g_pool_manager.default_config.enable_lock_free = false;
    g_pool_manager.default_config.enable_statistics = true;
    g_pool_manager.default_config.enable_profiling = false;
    
    // 初始化互斥锁
    if (pthread_mutex_init(&g_pool_manager.manager_mutex, NULL) != 0) {
        free(g_pool_manager.pools);
        return -1;
    }
    
    // 初始化线程本地存储键
    if (pthread_key_create(&g_pool_manager.thread_cache_key, 
                          (void(*)(void*))flush_thread_cache) != 0) {
        pthread_mutex_destroy(&g_pool_manager.manager_mutex);
        free(g_pool_manager.pools);
        return -1;
    }
    
    g_pool_manager.initialized = true;
    
    // 创建默认内存池
    struct memory_pool_opt* default_pool = memory_pool_opt_create(0, &g_pool_manager.default_config);
    if (!default_pool) {
        memory_pool_opt_manager_destroy();
        return -1;
    }
    
    g_default_pool_id = 0;
    
    return 0;
}

void memory_pool_opt_manager_destroy(void)
{
    if (!g_pool_manager.initialized) {
        return;
    }
    
    // 销毁所有内存池
    for (uint32_t i = 0; i < g_pool_manager.pool_count; i++) {
        if (g_pool_manager.pools[i]) {
            memory_pool_opt_destroy(g_pool_manager.pools[i]);
            g_pool_manager.pools[i] = NULL;
        }
    }
    
    // 释放内存池数组
    free(g_pool_manager.pools);
    g_pool_manager.pools = NULL;
    
    // 销毁互斥锁
    pthread_mutex_destroy(&g_pool_manager.manager_mutex);
    
    // 销毁线程本地存储键
    pthread_key_delete(g_pool_manager.thread_cache_key);
    
    memset(&g_pool_manager, 0, sizeof(g_pool_manager));
}

struct memory_pool_opt* memory_pool_opt_create(uint32_t pool_id, 
                                              const struct memory_pool_opt_config* config)
{
    if (!g_pool_manager.initialized) {
        return NULL;
    }
    
    // 检查池ID是否已存在
    if (pool_id != 0) {  // 0是保留的默认池ID
        pthread_mutex_lock(&g_pool_manager.manager_mutex);
        for (uint32_t i = 0; i < g_pool_manager.pool_count; i++) {
            if (g_pool_manager.pools[i] && g_pool_manager.pools[i]->pool_id == pool_id) {
                pthread_mutex_unlock(&g_pool_manager.manager_mutex);
                return NULL;
            }
        }
        pthread_mutex_unlock(&g_pool_manager.manager_mutex);
    }
    
    // 分配内存池结构
    struct memory_pool_opt* pool = calloc(1, sizeof(struct memory_pool_opt));
    if (!pool) {
        return NULL;
    }
    
    // 设置池ID
    if (pool_id == 0) {
        pool->pool_id = atomic_fetch_add(&g_next_pool_id, 1);
    } else {
        pool->pool_id = pool_id;
    }
    
    // 设置配置
    if (config) {
        pool->config = *config;
    } else {
        pool->config = g_pool_manager.default_config;
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool);
        return NULL;
    }
    
    // 初始化自旋锁
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        if (pthread_spin_init(&pool->spinlocks[i], PTHREAD_PROCESS_PRIVATE) != 0) {
            // 清理已初始化的自旋锁
            for (uint32_t j = 0; j < i; j++) {
                pthread_spin_destroy(&pool->spinlocks[j]);
            }
            pthread_mutex_destroy(&pool->mutex);
            free(pool);
            return NULL;
        }
    }
    
    // 初始化内存池
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        size_t block_size = get_block_size(i);
        if (!block_size) {
            continue;
        }
        
        uint32_t initial_count = pool->config.initial_block_counts[i];
        if (initial_count == 0) {
            continue;
        }
        
        // 分配初始块
        size_t total_size = initial_count * (block_size + sizeof(struct memory_pool_block_header));
        void* memory = malloc(total_size);
        if (!memory) {
            // 清理已分配的内存
            for (uint32_t j = 0; j < i; j++) {
                if (pool->free_lists[j]) {
                    // 注意：这里应该记录每个分配的内存块指针以便释放
                    // 简化实现，实际应该更复杂
                }
            }
            
            // 清理自旋锁
            for (uint32_t j = 0; j < MEMORY_POOL_BLOCK_SIZE_COUNT; j++) {
                pthread_spin_destroy(&pool->spinlocks[j]);
            }
            pthread_mutex_destroy(&pool->mutex);
            free(pool);
            return NULL;
        }
        
        // 初始化块
        char* block_ptr = (char*)memory;
        for (uint32_t j = 0; j < initial_count; j++) {
            struct memory_pool_block_header* header = (struct memory_pool_block_header*)block_ptr;
            init_block_header(header, i, pool->pool_id);
            
            // 添加到空闲列表
            header->next = pool->free_lists[i];
            pool->free_lists[i] = header;
            
            block_ptr += block_size + sizeof(struct memory_pool_block_header);
        }
        
        // 更新统计
        pool->block_counts[i] = initial_count;
        pool->total_size += total_size;
    }
    
    // 设置最后调整时间
    pool->last_resize_time = get_time_ns();
    
    // 标记为已初始化
    pool->initialized = true;
    
    // 添加到管理器
    if (pool_id != 0) {
        pthread_mutex_lock(&g_pool_manager.manager_mutex);
        if (g_pool_manager.pool_count < g_pool_manager.max_pools) {
            g_pool_manager.pools[g_pool_manager.pool_count++] = pool;
        } else {
            pthread_mutex_unlock(&g_pool_manager.manager_mutex);
            memory_pool_opt_destroy(pool);
            return NULL;
        }
        pthread_mutex_unlock(&g_pool_manager.manager_mutex);
    }
    
    return pool;
}

void memory_pool_opt_destroy(struct memory_pool_opt* pool)
{
    if (!pool || !pool->initialized) {
        return;
    }
    
    // 从管理器中移除
    if (pool->pool_id != 0) {
        pthread_mutex_lock(&g_pool_manager.manager_mutex);
        for (uint32_t i = 0; i < g_pool_manager.pool_count; i++) {
            if (g_pool_manager.pools[i] && g_pool_manager.pools[i]->pool_id == pool->pool_id) {
                g_pool_manager.pools[i] = NULL;
                break;
            }
        }
        pthread_mutex_unlock(&g_pool_manager.manager_mutex);
    }
    
    // 释放所有内存
    // 注意：简化实现，实际应该记录每个分配的内存块指针
    // 这里只是示例，实际实现需要更复杂的内存管理
    
    // 销毁自旋锁
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        pthread_spin_destroy(&pool->spinlocks[i]);
    }
    
    // 销毁互斥锁
    pthread_mutex_destroy(&pool->mutex);
    
    // 释放内存池结构
    free(pool);
}

void* memory_pool_opt_alloc(struct memory_pool_opt* pool, size_t size)
{
    if (!pool || !pool->initialized || size == 0) {
        return NULL;
    }
    
    // 获取大小等级
    uint32_t size_class = get_size_class(size);
    size_t block_size = get_block_size(size_class);
    if (!block_size) {
        return NULL;
    }
    
    uint64_t start_time = 0;
    if (pool->config.enable_profiling) {
        start_time = get_time_ns();
    }
    
    // 尝试从线程本地缓存分配
    if (pool->config.enable_thread_cache && g_thread_cache.initialized) {
        if (g_thread_cache.free_lists[size_class] && g_thread_cache.cache_counts[size_class] > 0) {
            struct memory_pool_block_header* header = g_thread_cache.free_lists[size_class];
            g_thread_cache.free_lists[size_class] = header->next;
            g_thread_cache.cache_counts[size_class]--;
            
            // 验证块头部
            if (validate_block_header(header)) {
                void* ptr = get_block_data(header);
                
                // 更新统计
                if (pool->config.enable_statistics) {
                    pool->stats.total_allocations++;
                    pool->stats.current_allocations++;
                    pool->stats.total_allocated_bytes += block_size;
                    pool->stats.current_allocated_bytes += block_size;
                    if (pool->stats.current_allocated_bytes > pool->stats.peak_allocated_bytes) {
                        pool->stats.peak_allocated_bytes = pool->stats.current_allocated_bytes;
                    }
                    pool->stats.cache_hits++;
                    
                    if (pool->config.enable_profiling) {
                        uint64_t elapsed = get_time_ns() - start_time;
                        pool->stats.avg_alloc_time_ns = 
                            (pool->stats.avg_alloc_time_ns * (pool->stats.total_allocations - 1) + elapsed) / 
                            pool->stats.total_allocations;
                    }
                }
                
                return ptr;
            }
        }
    }
    
    // 从池中分配
    pthread_mutex_lock(&pool->mutex);
    
    // 检查是否有空闲块
    if (!pool->free_lists[size_class]) {
        // 尝试扩展池
        if (expand_pool(pool, size_class) != 0) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }
    }
    
    // 获取空闲块
    struct memory_pool_block_header* header = pool->free_lists[size_class];
    if (!header) {
        pthread_mutex_unlock(&pool->mutex);
        return NULL;
    }
    
    // 从空闲列表中移除
    pool->free_lists[size_class] = header->next;
    pool->used_block_counts[size_class]++;
    
    pthread_mutex_unlock(&pool->mutex);
    
    // 验证块头部
    if (!validate_block_header(header)) {
        return NULL;
    }
    
    void* ptr = get_block_data(header);
    
    // 更新统计
    if (pool->config.enable_statistics) {
        pool->stats.total_allocations++;
        pool->stats.current_allocations++;
        pool->stats.total_allocated_bytes += block_size;
        pool->stats.current_allocated_bytes += block_size;
        if (pool->stats.current_allocated_bytes > pool->stats.peak_allocated_bytes) {
            pool->stats.peak_allocated_bytes = pool->stats.current_allocated_bytes;
        }
        pool->stats.cache_misses++;
        
        if (pool->config.enable_profiling) {
            uint64_t elapsed = get_time_ns() - start_time;
            pool->stats.avg_alloc_time_ns = 
                (pool->stats.avg_alloc_time_ns * (pool->stats.total_allocations - 1) + elapsed) / 
                pool->stats.total_allocations;
        }
    }
    
    // 自动调整池大小
    auto_resize_pool(pool);
    
    return ptr;
}

void memory_pool_opt_free(struct memory_pool_opt* pool, void* ptr)
{
    if (!pool || !pool->initialized || !ptr) {
        return;
    }
    
    // 获取块头部
    struct memory_pool_block_header* header = get_block_header(ptr);
    if (!header) {
        return;
    }
    
    // 验证块头部
    if (!validate_block_header(header) || header->pool_id != pool->pool_id) {
        return;
    }
    
    uint32_t size_class = header->size_class;
    size_t block_size = get_block_size(size_class);
    if (!block_size) {
        return;
    }
    
    uint64_t start_time = 0;
    if (pool->config.enable_profiling) {
        start_time = get_time_ns();
    }
    
    // 尝试添加到线程本地缓存
    if (pool->config.enable_thread_cache && g_thread_cache.initialized) {
        if (g_thread_cache.cache_counts[size_class] < g_thread_cache.max_cache_counts[size_class]) {
            // 添加到线程本地缓存
            header->next = g_thread_cache.free_lists[size_class];
            g_thread_cache.free_lists[size_class] = header;
            g_thread_cache.cache_counts[size_class]++;
            
            // 更新统计
            if (pool->config.enable_statistics) {
                pool->stats.total_frees++;
                pool->stats.current_allocations--;
                pool->stats.current_allocated_bytes -= block_size;
                
                if (pool->config.enable_profiling) {
                    uint64_t elapsed = get_time_ns() - start_time;
                    pool->stats.avg_free_time_ns = 
                        (pool->stats.avg_free_time_ns * (pool->stats.total_frees - 1) + elapsed) / 
                        pool->stats.total_frees;
                }
            }
            
            return;
        }
    }
    
    // 添加到池的空闲列表
    pthread_mutex_lock(&pool->mutex);
    
    header->next = pool->free_lists[size_class];
    pool->free_lists[size_class] = header;
    pool->used_block_counts[size_class]--;
    
    pthread_mutex_unlock(&pool->mutex);
    
    // 更新统计
    if (pool->config.enable_statistics) {
        pool->stats.total_frees++;
        pool->stats.current_allocations--;
        pool->stats.current_allocated_bytes -= block_size;
        
        if (pool->config.enable_profiling) {
            uint64_t elapsed = get_time_ns() - start_time;
            pool->stats.avg_free_time_ns = 
                (pool->stats.avg_free_time_ns * (pool->stats.total_frees - 1) + elapsed) / 
                pool->stats.total_frees;
        }
    }
    
    // 自动调整池大小
    auto_resize_pool(pool);
}

void* memory_pool_opt_realloc(struct memory_pool_opt* pool, void* ptr, size_t new_size)
{
    if (!pool || !pool->initialized) {
        return NULL;
    }
    
    // 如果ptr为NULL，等同于alloc
    if (!ptr) {
        return memory_pool_opt_alloc(pool, new_size);
    }
    
    // 如果new_size为0，等同于free
    if (new_size == 0) {
        memory_pool_opt_free(pool, ptr);
        return NULL;
    }
    
    // 获取原始块大小
    struct memory_pool_block_header* header = get_block_header(ptr);
    if (!header || !validate_block_header(header)) {
        return NULL;
    }
    
    uint32_t old_size_class = header->size_class;
    size_t old_block_size = get_block_size(old_size_class);
    if (!old_block_size) {
        return NULL;
    }
    
    // 获取新的大小等级
    uint32_t new_size_class = get_size_class(new_size);
    size_t new_block_size = get_block_size(new_size_class);
    if (!new_block_size) {
        return NULL;
    }
    
    // 如果大小等级相同，直接返回原指针
    if (old_size_class == new_size_class) {
        return ptr;
    }
    
    // 分配新块
    void* new_ptr = memory_pool_opt_alloc(pool, new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    // 复制数据
    size_t copy_size = new_size < old_block_size ? new_size : old_block_size;
    memcpy(new_ptr, ptr, copy_size);
    
    // 释放旧块
    memory_pool_opt_free(pool, ptr);
    
    return new_ptr;
}

size_t memory_pool_opt_get_size(void* ptr)
{
    if (!ptr) {
        return 0;
    }
    
    struct memory_pool_block_header* header = get_block_header(ptr);
    if (!header || !validate_block_header(header)) {
        return 0;
    }
    
    return get_block_size(header->size_class);
}

bool memory_pool_opt_is_from_pool(void* ptr)
{
    if (!ptr) {
        return false;
    }
    
    struct memory_pool_block_header* header = get_block_header(ptr);
    return header && validate_block_header(header);
}

int memory_pool_opt_compact(struct memory_pool_opt* pool)
{
    if (!pool || !pool->initialized) {
        return -1;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    // 对每个大小等级进行收缩
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        shrink_pool(pool, i);
    }
    
    pthread_mutex_unlock(&pool->mutex);
    
    return 0;
}

int memory_pool_opt_defragment(struct memory_pool_opt* pool)
{
    if (!pool || !pool->initialized) {
        return -1;
    }
    
    // 简化实现，实际需要更复杂的碎片整理逻辑
    return memory_pool_opt_compact(pool);
}

void memory_pool_opt_get_stats(struct memory_pool_opt* pool, 
                               struct memory_pool_opt_stats* stats)
{
    if (!pool || !pool->initialized || !stats) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    *stats = pool->stats;
    
    // 计算碎片化比率
    size_t total_allocated = 0;
    size_t total_used = 0;
    
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        size_t block_size = get_block_size(i);
        if (!block_size) {
            continue;
        }
        
        total_allocated += pool->block_counts[i] * block_size;
        total_used += pool->used_block_counts[i] * block_size;
    }
    
    if (total_allocated > 0) {
        stats->utilization_ratio = (float)total_used / total_allocated;
        stats->fragmentation_ratio = 1.0f - stats->utilization_ratio;
    }
    
    pthread_mutex_unlock(&pool->mutex);
}

void memory_pool_opt_reset_stats(struct memory_pool_opt* pool)
{
    if (!pool || !pool->initialized) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    memset(&pool->stats, 0, sizeof(pool->stats));
    
    pthread_mutex_unlock(&pool->mutex);
}

void memory_pool_opt_print_stats(struct memory_pool_opt* pool)
{
    if (!pool || !pool->initialized) {
        return;
    }
    
    struct memory_pool_opt_stats stats;
    memory_pool_opt_get_stats(pool, &stats);
    
    compositor_log_info("Memory Pool Stats (ID: %u):", pool->pool_id);
    compositor_log_info("  Total allocations: %u", stats.total_allocations);
    compositor_log_info("  Total frees: %u", stats.total_frees);
    compositor_log_info("  Current allocations: %u", stats.current_allocations);
    compositor_log_info("  Total allocated bytes: %zu", stats.total_allocated_bytes);
    compositor_log_info("  Current allocated bytes: %zu", stats.current_allocated_bytes);
    compositor_log_info("  Peak allocated bytes: %zu", stats.peak_allocated_bytes);
    compositor_log_info("  Cache hits: %u", stats.cache_hits);
    compositor_log_info("  Cache misses: %u", stats.cache_misses);
    compositor_log_info("  Fragmentation ratio: %.2f%%", stats.fragmentation_ratio * 100);
    compositor_log_info("  Utilization ratio: %.2f%%", stats.utilization_ratio * 100);
    compositor_log_info("  Avg alloc time: %llu ns", stats.avg_alloc_time_ns);
    compositor_log_info("  Avg free time: %llu ns", stats.avg_free_time_ns);
    compositor_log_info("  Lock contentions: %u", stats.lock_contentions);
    compositor_log_info("  Pool expansions: %u", stats.pool_expansions);
    compositor_log_info("  Pool shrinks: %u", stats.pool_shrinks);
}

int memory_pool_opt_set_config(struct memory_pool_opt* pool, 
                               const struct memory_pool_opt_config* config)
{
    if (!pool || !pool->initialized || !config) {
        return -1;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    pool->config = *config;
    
    pthread_mutex_unlock(&pool->mutex);
    
    return 0;
}

void memory_pool_opt_get_config(struct memory_pool_opt* pool, 
                               struct memory_pool_opt_config* config)
{
    if (!pool || !pool->initialized || !config) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    *config = pool->config;
    
    pthread_mutex_unlock(&pool->mutex);
}

// ==================== 线程本地缓存API实现 ====================

int memory_pool_opt_thread_cache_init(void)
{
    return init_thread_cache();
}

void memory_pool_opt_thread_cache_destroy(void)
{
    if (!g_thread_cache.initialized) {
        return;
    }
    
    flush_thread_cache();
    
    memset(&g_thread_cache, 0, sizeof(g_thread_cache));
}

void* memory_pool_opt_thread_cache_alloc(size_t size)
{
    if (!g_thread_cache.initialized) {
        init_thread_cache();
    }
    
    // 获取大小等级
    uint32_t size_class = get_size_class(size);
    if (size_class >= MEMORY_POOL_BLOCK_SIZE_COUNT) {
        return NULL;
    }
    
    // 检查是否有空闲块
    if (g_thread_cache.free_lists[size_class] && g_thread_cache.cache_counts[size_class] > 0) {
        struct memory_pool_block_header* header = g_thread_cache.free_lists[size_class];
        g_thread_cache.free_lists[size_class] = header->next;
        g_thread_cache.cache_counts[size_class]--;
        
        // 验证块头部
        if (validate_block_header(header)) {
            return get_block_data(header);
        }
    }
    
    // 从默认池分配
    struct memory_pool_opt* pool = memory_pool_opt_get_default();
    if (!pool) {
        return NULL;
    }
    
    return memory_pool_opt_alloc(pool, size);
}

void memory_pool_opt_thread_cache_free(void* ptr)
{
    if (!ptr) {
        return;
    }
    
    if (!g_thread_cache.initialized) {
        init_thread_cache();
    }
    
    // 获取块头部
    struct memory_pool_block_header* header = get_block_header(ptr);
    if (!header || !validate_block_header(header)) {
        return;
    }
    
    uint32_t size_class = header->size_class;
    if (size_class >= MEMORY_POOL_BLOCK_SIZE_COUNT) {
        return;
    }
    
    // 检查缓存是否已满
    if (g_thread_cache.cache_counts[size_class] >= g_thread_cache.max_cache_counts[size_class]) {
        // 刷新缓存
        flush_thread_cache();
    }
    
    // 添加到线程本地缓存
    header->next = g_thread_cache.free_lists[size_class];
    g_thread_cache.free_lists[size_class] = header;
    g_thread_cache.cache_counts[size_class]++;
}

void memory_pool_opt_thread_cache_flush(void)
{
    if (!g_thread_cache.initialized) {
        return;
    }
    
    flush_thread_cache();
}

void memory_pool_opt_thread_cache_get_stats(struct memory_pool_opt_stats* stats)
{
    if (!stats) {
        return;
    }
    
    memset(stats, 0, sizeof(*stats));
    
    if (!g_thread_cache.initialized) {
        return;
    }
    
    // 计算缓存统计
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        stats->current_allocations += g_thread_cache.cache_counts[i];
        
        size_t block_size = get_block_size(i);
        if (block_size) {
            stats->current_allocated_bytes += g_thread_cache.cache_counts[i] * block_size;
        }
    }
}

// ==================== 内存池管理器API实现 ====================

struct memory_pool_opt* memory_pool_opt_manager_get_pool(uint32_t pool_id)
{
    if (!g_pool_manager.initialized) {
        return NULL;
    }
    
    if (pool_id == 0) {
        return memory_pool_opt_get_default();
    }
    
    pthread_mutex_lock(&g_pool_manager.manager_mutex);
    
    struct memory_pool_opt* pool = NULL;
    for (uint32_t i = 0; i < g_pool_manager.pool_count; i++) {
        if (g_pool_manager.pools[i] && g_pool_manager.pools[i]->pool_id == pool_id) {
            pool = g_pool_manager.pools[i];
            break;
        }
    }
    
    pthread_mutex_unlock(&g_pool_manager.manager_mutex);
    
    return pool;
}

int memory_pool_opt_manager_add_pool(struct memory_pool_opt* pool)
{
    if (!g_pool_manager.initialized || !pool) {
        return -1;
    }
    
    pthread_mutex_lock(&g_pool_manager.manager_mutex);
    
    int result = -1;
    if (g_pool_manager.pool_count < g_pool_manager.max_pools) {
        g_pool_manager.pools[g_pool_manager.pool_count++] = pool;
        result = 0;
    }
    
    pthread_mutex_unlock(&g_pool_manager.manager_mutex);
    
    return result;
}

int memory_pool_opt_manager_remove_pool(uint32_t pool_id)
{
    if (!g_pool_manager.initialized) {
        return -1;
    }
    
    pthread_mutex_lock(&g_pool_manager.manager_mutex);
    
    int result = -1;
    for (uint32_t i = 0; i < g_pool_manager.pool_count; i++) {
        if (g_pool_manager.pools[i] && g_pool_manager.pools[i]->pool_id == pool_id) {
            g_pool_manager.pools[i] = NULL;
            result = 0;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_pool_manager.manager_mutex);
    
    return result;
}

void memory_pool_opt_manager_get_stats(struct memory_pool_opt_stats* stats)
{
    if (!g_pool_manager.initialized || !stats) {
        return;
    }
    
    memset(stats, 0, sizeof(*stats));
    
    pthread_mutex_lock(&g_pool_manager.manager_mutex);
    
    // 聚合所有池的统计
    for (uint32_t i = 0; i < g_pool_manager.pool_count; i++) {
        if (g_pool_manager.pools[i]) {
            struct memory_pool_opt_stats pool_stats;
            memory_pool_opt_get_stats(g_pool_manager.pools[i], &pool_stats);
            
            stats->total_allocations += pool_stats.total_allocations;
            stats->total_frees += pool_stats.total_frees;
            stats->current_allocations += pool_stats.current_allocations;
            stats->total_allocated_bytes += pool_stats.total_allocated_bytes;
            stats->current_allocated_bytes += pool_stats.current_allocated_bytes;
            
            if (pool_stats.peak_allocated_bytes > stats->peak_allocated_bytes) {
                stats->peak_allocated_bytes = pool_stats.peak_allocated_bytes;
            }
            
            stats->cache_hits += pool_stats.cache_hits;
            stats->cache_misses += pool_stats.cache_misses;
            stats->lock_contentions += pool_stats.lock_contentions;
            stats->pool_expansions += pool_stats.pool_expansions;
            stats->pool_shrinks += pool_stats.pool_shrinks;
            
            // 平均时间需要加权计算
            if (pool_stats.total_allocations > 0) {
                stats->avg_alloc_time_ns += pool_stats.avg_alloc_time_ns * pool_stats.total_allocations;
            }
            if (pool_stats.total_frees > 0) {
                stats->avg_free_time_ns += pool_stats.avg_free_time_ns * pool_stats.total_frees;
            }
        }
    }
    
    // 计算加权平均时间
    if (stats->total_allocations > 0) {
        stats->avg_alloc_time_ns /= stats->total_allocations;
    }
    if (stats->total_frees > 0) {
        stats->avg_free_time_ns /= stats->total_frees;
    }
    
    // 计算全局碎片化比率
    if (stats->total_allocated_bytes > 0) {
        stats->utilization_ratio = (float)stats->current_allocated_bytes / stats->total_allocated_bytes;
        stats->fragmentation_ratio = 1.0f - stats->utilization_ratio;
    }
    
    pthread_mutex_unlock(&g_pool_manager.manager_mutex);
}

void memory_pool_opt_manager_reset_stats(void)
{
    if (!g_pool_manager.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_pool_manager.manager_mutex);
    
    for (uint32_t i = 0; i < g_pool_manager.pool_count; i++) {
        if (g_pool_manager.pools[i]) {
            memory_pool_opt_reset_stats(g_pool_manager.pools[i]);
        }
    }
    
    memset(&g_pool_manager.global_stats, 0, sizeof(g_pool_manager.global_stats));
    
    pthread_mutex_unlock(&g_pool_manager.manager_mutex);
}

void memory_pool_opt_manager_print_stats(void)
{
    if (!g_pool_manager.initialized) {
        return;
    }
    
    struct memory_pool_opt_stats stats;
    memory_pool_opt_manager_get_stats(&stats);
    
    compositor_log_info("Global Memory Pool Manager Stats:");
    compositor_log_info("  Total pools: %u", g_pool_manager.pool_count);
    compositor_log_info("  Total allocations: %u", stats.total_allocations);
    compositor_log_info("  Total frees: %u", stats.total_frees);
    compositor_log_info("  Current allocations: %u", stats.current_allocations);
    compositor_log_info("  Total allocated bytes: %zu", stats.total_allocated_bytes);
    compositor_log_info("  Current allocated bytes: %zu", stats.current_allocated_bytes);
    compositor_log_info("  Peak allocated bytes: %zu", stats.peak_allocated_bytes);
    compositor_log_info("  Cache hits: %u", stats.cache_hits);
    compositor_log_info("  Cache misses: %u", stats.cache_misses);
    compositor_log_info("  Fragmentation ratio: %.2f%%", stats.fragmentation_ratio * 100);
    compositor_log_info("  Utilization ratio: %.2f%%", stats.utilization_ratio * 100);
    compositor_log_info("  Avg alloc time: %llu ns", stats.avg_alloc_time_ns);
    compositor_log_info("  Avg free time: %llu ns", stats.avg_free_time_ns);
    compositor_log_info("  Lock contentions: %u", stats.lock_contentions);
    compositor_log_info("  Pool expansions: %u", stats.pool_expansions);
    compositor_log_info("  Pool shrinks: %u", stats.pool_shrinks);
}

// ==================== 便利API实现 ====================

void* memory_pool_opt_alloc_fast(size_t size)
{
    struct memory_pool_opt* pool = memory_pool_opt_get_default();
    if (!pool) {
        return NULL;
    }
    
    return memory_pool_opt_alloc(pool, size);
}

void memory_pool_opt_free_fast(void* ptr)
{
    if (!ptr) {
        return;
    }
    
    // 获取块头部
    struct memory_pool_block_header* header = get_block_header(ptr);
    if (!header || !validate_block_header(header)) {
        return;
    }
    
    // 获取所属池
    struct memory_pool_opt* pool = memory_pool_opt_manager_get_pool(header->pool_id);
    if (!pool) {
        return;
    }
    
    memory_pool_opt_free(pool, ptr);
}

void* memory_pool_opt_realloc_fast(void* ptr, size_t new_size)
{
    if (!ptr) {
        return memory_pool_opt_alloc_fast(new_size);
    }
    
    if (new_size == 0) {
        memory_pool_opt_free_fast(ptr);
        return NULL;
    }
    
    // 获取块头部
    struct memory_pool_block_header* header = get_block_header(ptr);
    if (!header || !validate_block_header(header)) {
        return NULL;
    }
    
    // 获取所属池
    struct memory_pool_opt* pool = memory_pool_opt_manager_get_pool(header->pool_id);
    if (!pool) {
        return NULL;
    }
    
    return memory_pool_opt_realloc(pool, ptr, new_size);
}

struct memory_pool_opt* memory_pool_opt_get_default(void)
{
    if (!g_pool_manager.initialized) {
        return NULL;
    }
    
    return memory_pool_opt_manager_get_pool(g_default_pool_id);
}

int memory_pool_opt_set_default(uint32_t pool_id)
{
    if (!g_pool_manager.initialized) {
        return -1;
    }
    
    struct memory_pool_opt* pool = memory_pool_opt_manager_get_pool(pool_id);
    if (!pool) {
        return -1;
    }
    
    g_default_pool_id = pool_id;
    return 0;
}