#include "compositor_memory_pool.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>

#define LOG_TAG "MemoryPool"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ==================== 工具函数 ====================

// 获取当前时间（毫秒）
uint64_t memory_pool_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 计算对齐大小
size_t memory_pool_align_size(size_t size) {
    return (size + 7) & ~7; // 8字节对齐
}

// ==================== 内存池实现 ====================

// 初始化内存池
struct memory_pool* memory_pool_create(size_t block_size, uint32_t block_count) {
    if (block_size == 0 || block_count == 0) {
        LOGE("内存池参数无效: 块大小=%zu, 块数量=%u", block_size, block_count);
        return NULL;
    }
    
    // 分配内存池结构
    struct memory_pool* pool = malloc(sizeof(struct memory_pool));
    if (!pool) {
        LOGE("分配内存池结构失败");
        return NULL;
    }
    
    // 对齐块大小
    block_size = memory_pool_align_size(block_size);
    
    // 分配内存块
    size_t total_size = block_size * block_count;
    void* memory = malloc(total_size);
    if (!memory) {
        LOGE("分配内存池内存失败: %zu 字节", total_size);
        free(pool);
        return NULL;
    }
    
    // 初始化内存池
    memset(pool, 0, sizeof(struct memory_pool));
    pool->memory = memory;
    pool->block_size = block_size;
    pool->block_count = block_count;
    pool->free_blocks = block_count;
    pool->used_blocks = 0;
    pool->free_list = NULL;
    
    // 初始化互斥锁
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        LOGE("初始化内存池互斥锁失败");
        free(memory);
        free(pool);
        return NULL;
    }
    
    // 初始化空闲链表
    char* block = (char*)memory;
    for (uint32_t i = 0; i < block_count; i++) {
        struct memory_block_header* header = (struct memory_block_header*)block;
        header->next = pool->free_list;
        pool->free_list = header;
        block += block_size;
    }
    
    LOGI("创建内存池: 块大小=%zu, 块数量=%u, 总大小=%zu", 
         block_size, block_count, total_size);
    
    return pool;
}

// 销毁内存池
void memory_pool_destroy(struct memory_pool* pool) {
    if (!pool) {
        return;
    }
    
    // 销毁互斥锁
    pthread_mutex_destroy(&pool->mutex);
    
    // 释放内存
    if (pool->memory) {
        free(pool->memory);
    }
    
    LOGI("销毁内存池: 块大小=%zu, 块数量=%u", 
         pool->block_size, pool->block_count);
    
    // 释放内存池结构
    free(pool);
}

// 从内存池分配内存
void* memory_pool_alloc(struct memory_pool* pool) {
    if (!pool) {
        LOGE("内存池不能为空");
        return NULL;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    if (pool->free_blocks == 0) {
        pthread_mutex_unlock(&pool->mutex);
        LOGE("内存池已满: 块大小=%zu, 块数量=%u", 
             pool->block_size, pool->block_count);
        return NULL;
    }
    
    // 从空闲链表取出一个块
    struct memory_block_header* header = pool->free_list;
    pool->free_list = header->next;
    
    // 更新统计
    pool->free_blocks--;
    pool->used_blocks++;
    
    pthread_mutex_unlock(&pool->mutex);
    
    // 返回块数据部分
    void* block = (char*)header + sizeof(struct memory_block_header);
    
    LOGI("从内存池分配内存: 块大小=%zu, 剩余空闲块=%u", 
         pool->block_size, pool->free_blocks);
    
    return block;
}

// 释放内存到内存池
void memory_pool_free(struct memory_pool* pool, void* block) {
    if (!pool || !block) {
        LOGE("内存池或块不能为空");
        return;
    }
    
    // 获取块头部
    struct memory_block_header* header = (struct memory_block_header*)
                                         ((char*)block - sizeof(struct memory_block_header));
    
    pthread_mutex_lock(&pool->mutex);
    
    // 将块添加到空闲链表
    header->next = pool->free_list;
    pool->free_list = header;
    
    // 更新统计
    pool->free_blocks++;
    pool->used_blocks--;
    
    pthread_mutex_unlock(&pool->mutex);
    
    LOGI("释放内存到内存池: 块大小=%zu, 剩余空闲块=%u", 
         pool->block_size, pool->free_blocks);
}

// 调整内存池大小
int memory_pool_resize(struct memory_pool* pool, uint32_t new_block_count) {
    if (!pool || new_block_count == 0) {
        LOGE("内存池参数无效: 块数量=%u", new_block_count);
        return -1;
    }
    
    if (new_block_count == pool->block_count) {
        LOGI("内存池大小无需调整: 块数量=%u", new_block_count);
        return 0;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    // 检查是否可以缩小
    if (new_block_count < pool->block_count && 
        pool->used_blocks > new_block_count) {
        pthread_mutex_unlock(&pool->mutex);
        LOGE("无法缩小内存池: 已使用块数=%u, 目标块数=%u", 
             pool->used_blocks, new_block_count);
        return -1;
    }
    
    // 计算新的大小
    size_t old_total_size = pool->block_size * pool->block_count;
    size_t new_total_size = pool->block_size * new_block_count;
    
    // 重新分配内存
    void* new_memory = realloc(pool->memory, new_total_size);
    if (!new_memory) {
        pthread_mutex_unlock(&pool->mutex);
        LOGE("重新分配内存池内存失败: %zu 字节", new_total_size);
        return -1;
    }
    
    pool->memory = new_memory;
    
    // 如果扩大内存池，需要初始化新的块
    if (new_block_count > pool->block_count) {
        char* block = (char*)new_memory + old_total_size;
        for (uint32_t i = pool->block_count; i < new_block_count; i++) {
            struct memory_block_header* header = (struct memory_block_header*)block;
            header->next = pool->free_list;
            pool->free_list = header;
            block += pool->block_size;
        }
        
        pool->free_blocks += (new_block_count - pool->block_count);
    } else {
        // 缩小内存池，需要从空闲链表中移除多余的块
        // 这里简化实现，不实际移除
        pool->free_blocks -= (pool->block_count - new_block_count);
    }
    
    pool->block_count = new_block_count;
    
    pthread_mutex_unlock(&pool->mutex);
    
    LOGI("调整内存池大小: 块大小=%zu, 旧块数=%u, 新块数=%u", 
         pool->block_size, pool->block_count, new_block_count);
    
    return 0;
}

// 获取内存池使用情况
void memory_pool_get_stats(struct memory_pool* pool, struct memory_pool_stats* stats) {
    if (!pool || !stats) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    stats->block_size = pool->block_size;
    stats->block_count = pool->block_count;
    stats->free_blocks = pool->free_blocks;
    stats->used_blocks = pool->used_blocks;
    stats->total_size = pool->block_size * pool->block_count;
    stats->used_size = pool->block_size * pool->used_blocks;
    stats->free_size = pool->block_size * pool->free_blocks;
    
    pthread_mutex_unlock(&pool->mutex);
}

// 内存池碎片整理
int memory_pool_defragment(struct memory_pool* pool) {
    if (!pool) {
        LOGE("内存池不能为空");
        return -1;
    }
    
    // 对于简单的内存池，碎片整理不需要做任何操作
    // 这里只是更新统计信息
    
    LOGI("内存池碎片整理完成: 块大小=%zu, 块数量=%u", 
         pool->block_size, pool->block_count);
    
    return 0;
}

// ==================== 智能内存池实现 ====================

// 创建智能内存池
struct smart_memory_pool* smart_memory_pool_create(size_t min_block_size, 
                                                   size_t max_block_size,
                                                   uint32_t initial_block_count) {
    if (min_block_size == 0 || max_block_size == 0 || 
        min_block_size > max_block_size || initial_block_count == 0) {
        LOGE("智能内存池参数无效: 最小块大小=%zu, 最大块大小=%zu, 初始块数=%u", 
             min_block_size, max_block_size, initial_block_count);
        return NULL;
    }
    
    // 分配智能内存池结构
    struct smart_memory_pool* pool = malloc(sizeof(struct smart_memory_pool));
    if (!pool) {
        LOGE("分配智能内存池结构失败");
        return NULL;
    }
    
    // 初始化智能内存池
    memset(pool, 0, sizeof(struct smart_memory_pool));
    pool->min_block_size = min_block_size;
    pool->max_block_size = max_block_size;
    pool->growth_factor = 1.5; // 默认增长因子
    pool->shrink_threshold = 0.25; // 默认收缩阈值
    pool->auto_resize = true; // 默认启用自动调整大小
    pool->last_resize_time = memory_pool_get_time();
    pool->resize_interval = 5000; // 默认5秒调整一次大小
    
    // 初始化互斥锁
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        LOGE("初始化智能内存池互斥锁失败");
        free(pool);
        return NULL;
    }
    
    // 创建初始内存池
    pool->pool = memory_pool_create(min_block_size, initial_block_count);
    if (!pool->pool) {
        LOGE("创建初始内存池失败");
        pthread_mutex_destroy(&pool->mutex);
        free(pool);
        return NULL;
    }
    
    LOGI("创建智能内存池: 最小块大小=%zu, 最大块大小=%zu, 初始块数=%u", 
         min_block_size, max_block_size, initial_block_count);
    
    return pool;
}

// 销毁智能内存池
void smart_memory_pool_destroy(struct smart_memory_pool* pool) {
    if (!pool) {
        return;
    }
    
    // 销毁内存池
    if (pool->pool) {
        memory_pool_destroy(pool->pool);
    }
    
    // 销毁互斥锁
    pthread_mutex_destroy(&pool->mutex);
    
    LOGI("销毁智能内存池: 最小块大小=%zu, 最大块大小=%zu", 
         pool->min_block_size, pool->max_block_size);
    
    // 释放智能内存池结构
    free(pool);
}

// 从智能内存池分配内存
void* smart_memory_pool_alloc(struct smart_memory_pool* pool, size_t size) {
    if (!pool) {
        LOGE("智能内存池不能为空");
        return NULL;
    }
    
    // 检查大小是否在范围内
    if (size > pool->max_block_size) {
        LOGE("请求大小超出最大块大小: 请求=%zu, 最大=%zu", 
             size, pool->max_block_size);
        return NULL;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    // 如果请求大小小于当前块大小，直接分配
    if (size <= pool->pool->block_size) {
        pthread_mutex_unlock(&pool->mutex);
        return memory_pool_alloc(pool->pool);
    }
    
    // 请求大小大于当前块大小，需要调整内存池大小
    size_t new_block_size = memory_pool_align_size(size);
    uint32_t new_block_count = (uint32_t)(pool->pool->block_count * pool->growth_factor);
    
    // 确保块数量至少为1
    if (new_block_count == 0) {
        new_block_count = 1;
    }
    
    // 调整内存池大小
    int result = memory_pool_resize(pool->pool, new_block_count);
    if (result != 0) {
        pthread_mutex_unlock(&pool->mutex);
        LOGE("调整智能内存池大小失败");
        return NULL;
    }
    
    pthread_mutex_unlock(&pool->mutex);
    
    // 分配内存
    return memory_pool_alloc(pool->pool);
}

// 释放内存到智能内存池
void smart_memory_pool_free(struct smart_memory_pool* pool, void* block) {
    if (!pool || !block) {
        LOGE("智能内存池或块不能为空");
        return;
    }
    
    // 释放内存到内存池
    memory_pool_free(pool->pool, block);
    
    // 检查是否需要自动调整大小
    if (pool->auto_resize) {
        uint64_t current_time = memory_pool_get_time();
        if (current_time - pool->last_resize_time >= pool->resize_interval) {
            smart_memory_pool_auto_resize(pool);
            pool->last_resize_time = current_time;
        }
    }
}

// 智能内存池自动调整大小
void smart_memory_pool_auto_resize(struct smart_memory_pool* pool) {
    if (!pool) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    // 获取当前内存池统计
    struct memory_pool_stats stats;
    memory_pool_get_stats(pool->pool, &stats);
    
    // 计算使用率
    float usage_ratio = (float)stats.used_blocks / stats.block_count;
    
    // 如果使用率低于收缩阈值，缩小内存池
    if (usage_ratio < pool->shrink_threshold && stats.block_count > 1) {
        uint32_t new_block_count = (uint32_t)(stats.block_count * 0.75);
        if (new_block_count < 1) {
            new_block_count = 1;
        }
        
        int result = memory_pool_resize(pool->pool, new_block_count);
        if (result == 0) {
            LOGI("智能内存池自动缩小: 旧块数=%u, 新块数=%u, 使用率=%.2f", 
                 stats.block_count, new_block_count, usage_ratio);
        } else {
            LOGE("智能内存池自动缩小失败");
        }
    }
    // 如果使用率高于80%，扩大内存池
    else if (usage_ratio > 0.8) {
        uint32_t new_block_count = (uint32_t)(stats.block_count * pool->growth_factor);
        
        int result = memory_pool_resize(pool->pool, new_block_count);
        if (result == 0) {
            LOGI("智能内存池自动扩大: 旧块数=%u, 新块数=%u, 使用率=%.2f", 
                 stats.block_count, new_block_count, usage_ratio);
        } else {
            LOGE("智能内存池自动扩大失败");
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
}

// 获取智能内存池使用情况
void smart_memory_pool_get_stats(struct smart_memory_pool* pool, 
                                 struct smart_memory_pool_stats* stats) {
    if (!pool || !stats) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    // 获取基础内存池统计
    memory_pool_get_stats(pool->pool, &stats->base_stats);
    
    // 添加智能内存池特有统计
    stats->min_block_size = pool->min_block_size;
    stats->max_block_size = pool->max_block_size;
    stats->growth_factor = pool->growth_factor;
    stats->shrink_threshold = pool->shrink_threshold;
    stats->auto_resize = pool->auto_resize;
    stats->resize_count = pool->resize_count;
    stats->last_resize_time = pool->last_resize_time;
    
    pthread_mutex_unlock(&pool->mutex);
}

// ==================== 线程本地内存池实现 ====================

// 线程退出时的清理函数
static void thread_local_memory_pool_cleanup(void* arg) {
    struct thread_local_memory_pool* pool = (struct thread_local_memory_pool*)arg;
    if (pool) {
        thread_local_memory_pool_destroy(pool);
    }
}

// 初始化线程本地内存池
struct thread_local_memory_pool* thread_local_memory_pool_init(void) {
    // 检查是否已经初始化
    if (g_thread_local_pool) {
        return g_thread_local_pool;
    }
    
    // 分配线程本地内存池结构
    struct thread_local_memory_pool* pool = malloc(sizeof(struct thread_local_memory_pool));
    if (!pool) {
        LOGE("分配线程本地内存池结构失败");
        return NULL;
    }
    
    // 初始化线程本地内存池
    memset(pool, 0, sizeof(struct thread_local_memory_pool));
    
    // 创建不同大小的内存池
    for (int i = 0; i < MEMORY_POOL_SIZE_COUNT; i++) {
        size_t block_size = g_memory_pool_sizes[i];
        uint32_t block_count = 16; // 默认每个池16个块
        
        pool->pools[i] = memory_pool_create(block_size, block_count);
        if (!pool->pools[i]) {
            LOGE("创建线程本地内存池失败: 块大小=%zu", block_size);
            
            // 销毁已创建的内存池
            for (int j = 0; j < i; j++) {
                memory_pool_destroy(pool->pools[j]);
            }
            
            free(pool);
            return NULL;
        }
    }
    
    // 设置线程退出时的清理函数
    pthread_key_create(&g_thread_local_pool_key, thread_local_memory_pool_cleanup);
    pthread_setspecific(g_thread_local_pool_key, pool);
    
    g_thread_local_pool = pool;
    
    LOGI("初始化线程本地内存池完成");
    
    return pool;
}

// 销毁线程本地内存池
void thread_local_memory_pool_destroy(struct thread_local_memory_pool* pool) {
    if (!pool) {
        return;
    }
    
    // 销毁所有内存池
    for (int i = 0; i < MEMORY_POOL_SIZE_COUNT; i++) {
        if (pool->pools[i]) {
            memory_pool_destroy(pool->pools[i]);
        }
    }
    
    LOGI("销毁线程本地内存池完成");
    
    // 释放线程本地内存池结构
    free(pool);
    
    // 清理线程本地存储
    g_thread_local_pool = NULL;
}

// 从线程本地内存池分配内存
void* thread_local_memory_pool_alloc(size_t size) {
    // 获取线程本地内存池
    struct thread_local_memory_pool* pool = g_thread_local_pool;
    if (!pool) {
        pool = thread_local_memory_pool_init();
        if (!pool) {
            LOGE("初始化线程本地内存池失败");
            return NULL;
        }
    }
    
    // 找到合适的内存池
    int pool_index = -1;
    for (int i = 0; i < MEMORY_POOL_SIZE_COUNT; i++) {
        if (size <= g_memory_pool_sizes[i]) {
            pool_index = i;
            break;
        }
    }
    
    if (pool_index == -1) {
        LOGE("请求大小超出线程本地内存池范围: %zu", size);
        return NULL;
    }
    
    // 从合适的内存池分配内存
    return memory_pool_alloc(pool->pools[pool_index]);
}

// 释放内存到线程本地内存池
void thread_local_memory_pool_free(void* block, size_t size) {
    if (!block) {
        return;
    }
    
    // 获取线程本地内存池
    struct thread_local_memory_pool* pool = g_thread_local_pool;
    if (!pool) {
        LOGE("线程本地内存池未初始化");
        return;
    }
    
    // 找到合适的内存池
    int pool_index = -1;
    for (int i = 0; i < MEMORY_POOL_SIZE_COUNT; i++) {
        if (size <= g_memory_pool_sizes[i]) {
            pool_index = i;
            break;
        }
    }
    
    if (pool_index == -1) {
        LOGE("请求大小超出线程本地内存池范围: %zu", size);
        return;
    }
    
    // 释放内存到合适的内存池
    memory_pool_free(pool->pools[pool_index], block);
}

// ==================== 内存池缓存实现 ====================

// 初始化内存池缓存
int memory_pool_cache_init(struct memory_pool_cache* cache, uint32_t max_pools) {
    if (!cache || max_pools == 0) {
        LOGE("内存池缓存参数无效: 最大池数=%u", max_pools);
        return -1;
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&cache->mutex, NULL) != 0) {
        LOGE("初始化内存池缓存互斥锁失败");
        return -1;
    }
    
    // 分配池数组
    cache->pools = malloc(sizeof(struct memory_pool*) * max_pools);
    if (!cache->pools) {
        LOGE("分配内存池缓存数组失败");
        pthread_mutex_destroy(&cache->mutex);
        return -1;
    }
    
    // 初始化缓存
    memset(cache, 0, sizeof(struct memory_pool_cache));
    cache->pools = malloc(sizeof(struct memory_pool*) * max_pools);
    cache->max_pools = max_pools;
    cache->pool_count = 0;
    cache->hit_count = 0;
    cache->miss_count = 0;
    
    LOGI("初始化内存池缓存完成: 最大池数=%u", max_pools);
    
    return 0;
}

// 销毁内存池缓存
void memory_pool_cache_destroy(struct memory_pool_cache* cache) {
    if (!cache) {
        return;
    }
    
    // 销毁所有内存池
    for (uint32_t i = 0; i < cache->pool_count; i++) {
        if (cache->pools[i]) {
            memory_pool_destroy(cache->pools[i]);
        }
    }
    
    // 释放池数组
    if (cache->pools) {
        free(cache->pools);
    }
    
    // 销毁互斥锁
    pthread_mutex_destroy(&cache->mutex);
    
    LOGI("销毁内存池缓存完成");
}

// 添加内存池到缓存
int memory_pool_cache_add(struct memory_pool_cache* cache, struct memory_pool* pool) {
    if (!cache || !pool) {
        LOGE("内存池缓存或内存池不能为空");
        return -1;
    }
    
    pthread_mutex_lock(&cache->mutex);
    
    // 检查是否已满
    if (cache->pool_count >= cache->max_pools) {
        pthread_mutex_unlock(&cache->mutex);
        LOGE("内存池缓存已满: 当前=%u, 最大=%u", 
             cache->pool_count, cache->max_pools);
        return -1;
    }
    
    // 添加到缓存
    cache->pools[cache->pool_count] = pool;
    cache->pool_count++;
    
    pthread_mutex_unlock(&cache->mutex);
    
    LOGI("添加内存池到缓存: 块大小=%zu, 块数量=%u", 
         pool->block_size, pool->block_count);
    
    return 0;
}

// 从缓存获取内存池
struct memory_pool* memory_pool_cache_get(struct memory_pool_cache* cache, size_t block_size) {
    if (!cache) {
        LOGE("内存池缓存不能为空");
        return NULL;
    }
    
    pthread_mutex_lock(&cache->mutex);
    
    // 查找匹配的内存池
    struct memory_pool* pool = NULL;
    for (uint32_t i = 0; i < cache->pool_count; i++) {
        if (cache->pools[i] && cache->pools[i]->block_size >= block_size) {
            pool = cache->pools[i];
            cache->hit_count++;
            break;
        }
    }
    
    if (!pool) {
        cache->miss_count++;
    }
    
    pthread_mutex_unlock(&cache->mutex);
    
    LOGI("从缓存获取内存池: 块大小=%zu, 结果=%s", 
         block_size, pool ? "命中" : "未命中");
    
    return pool;
}

// 获取内存池缓存统计
void memory_pool_cache_get_stats(struct memory_pool_cache* cache, 
                                struct memory_pool_cache_stats* stats) {
    if (!cache || !stats) {
        return;
    }
    
    pthread_mutex_lock(&cache->mutex);
    
    stats->max_pools = cache->max_pools;
    stats->pool_count = cache->pool_count;
    stats->hit_count = cache->hit_count;
    stats->miss_count = cache->miss_count;
    
    // 计算命中率
    uint32_t total_requests = cache->hit_count + cache->miss_count;
    stats->hit_rate = total_requests > 0 ? 
                     (float)cache->hit_count / total_requests : 0.0f;
    
    pthread_mutex_unlock(&cache->mutex);
}

// ==================== 内存池优化器实现 ====================

// 初始化内存池优化器
int memory_pool_optimizer_init(struct memory_pool_optimizer* optimizer, 
                               uint32_t max_pools, uint32_t update_interval) {
    if (!optimizer || max_pools == 0 || update_interval == 0) {
        LOGE("内存池优化器参数无效: 最大池数=%u, 更新间隔=%u", 
             max_pools, update_interval);
        return -1;
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&optimizer->mutex, NULL) != 0) {
        LOGE("初始化内存池优化器互斥锁失败");
        return -1;
    }
    
    // 分配池数组
    optimizer->pools = malloc(sizeof(struct smart_memory_pool*) * max_pools);
    if (!optimizer->pools) {
        LOGE("分配内存池优化器数组失败");
        pthread_mutex_destroy(&optimizer->mutex);
        return -1;
    }
    
    // 初始化优化器
    memset(optimizer, 0, sizeof(struct memory_pool_optimizer));
    optimizer->pools = malloc(sizeof(struct smart_memory_pool*) * max_pools);
    optimizer->max_pools = max_pools;
    optimizer->pool_count = 0;
    optimizer->update_interval = update_interval;
    optimizer->last_update_time = memory_pool_get_time();
    optimizer->enabled = true;
    
    LOGI("初始化内存池优化器完成: 最大池数=%u, 更新间隔=%u", 
         max_pools, update_interval);
    
    return 0;
}

// 销毁内存池优化器
void memory_pool_optimizer_destroy(struct memory_pool_optimizer* optimizer) {
    if (!optimizer) {
        return;
    }
    
    // 销毁所有智能内存池
    for (uint32_t i = 0; i < optimizer->pool_count; i++) {
        if (optimizer->pools[i]) {
            smart_memory_pool_destroy(optimizer->pools[i]);
        }
    }
    
    // 释放池数组
    if (optimizer->pools) {
        free(optimizer->pools);
    }
    
    // 销毁互斥锁
    pthread_mutex_destroy(&optimizer->mutex);
    
    LOGI("销毁内存池优化器完成");
}

// 添加智能内存池到优化器
int memory_pool_optimizer_add_pool(struct memory_pool_optimizer* optimizer, 
                                  struct smart_memory_pool* pool) {
    if (!optimizer || !pool) {
        LOGE("内存池优化器或智能内存池不能为空");
        return -1;
    }
    
    pthread_mutex_lock(&optimizer->mutex);
    
    // 检查是否已满
    if (optimizer->pool_count >= optimizer->max_pools) {
        pthread_mutex_unlock(&optimizer->mutex);
        LOGE("内存池优化器已满: 当前=%u, 最大=%u", 
             optimizer->pool_count, optimizer->max_pools);
        return -1;
    }
    
    // 添加到优化器
    optimizer->pools[optimizer->pool_count] = pool;
    optimizer->pool_count++;
    
    pthread_mutex_unlock(&optimizer->mutex);
    
    LOGI("添加智能内存池到优化器: 最小块大小=%zu, 最大块大小=%zu", 
         pool->min_block_size, pool->max_block_size);
    
    return 0;
}

// 更新内存池优化器
void memory_pool_optimizer_update(struct memory_pool_optimizer* optimizer) {
    if (!optimizer || !optimizer->enabled) {
        return;
    }
    
    uint64_t current_time = memory_pool_get_time();
    if (current_time - optimizer->last_update_time < optimizer->update_interval) {
        return;
    }
    
    pthread_mutex_lock(&optimizer->mutex);
    
    // 更新所有智能内存池
    for (uint32_t i = 0; i < optimizer->pool_count; i++) {
        if (optimizer->pools[i]) {
            smart_memory_pool_auto_resize(optimizer->pools[i]);
        }
    }
    
    optimizer->last_update_time = current_time;
    
    pthread_mutex_unlock(&optimizer->mutex);
    
    LOGI("更新内存池优化器完成");
}

// 获取内存池优化器统计
void memory_pool_optimizer_get_stats(struct memory_pool_optimizer* optimizer, 
                                    struct memory_pool_optimizer_stats* stats) {
    if (!optimizer || !stats) {
        return;
    }
    
    pthread_mutex_lock(&optimizer->mutex);
    
    stats->max_pools = optimizer->max_pools;
    stats->pool_count = optimizer->pool_count;
    stats->update_interval = optimizer->update_interval;
    stats->last_update_time = optimizer->last_update_time;
    stats->enabled = optimizer->enabled;
    
    // 计算总使用情况
    stats->total_blocks = 0;
    stats->total_used_blocks = 0;
    stats->total_size = 0;
    stats->total_used_size = 0;
    
    for (uint32_t i = 0; i < optimizer->pool_count; i++) {
        if (optimizer->pools[i]) {
            struct smart_memory_pool_stats pool_stats;
            smart_memory_pool_get_stats(optimizer->pools[i], &pool_stats);
            
            stats->total_blocks += pool_stats.base_stats.block_count;
            stats->total_used_blocks += pool_stats.base_stats.used_blocks;
            stats->total_size += pool_stats.base_stats.total_size;
            stats->total_used_size += pool_stats.base_stats.used_size;
        }
    }
    
    // 计算总使用率
    stats->total_usage_ratio = stats->total_blocks > 0 ? 
                               (float)stats->total_used_blocks / stats->total_blocks : 0.0f;
    
    pthread_mutex_unlock(&optimizer->mutex);
}

// ==================== 分层内存池实现 ====================

// 初始化分层内存池
int layered_memory_pool_init(struct layered_memory_pool* pool, 
                             uint32_t small_pool_count, uint32_t medium_pool_count,
                             uint32_t large_pool_count) {
    if (!pool) {
        LOGE("分层内存池不能为空");
        return -1;
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        LOGE("初始化分层内存池互斥锁失败");
        return -1;
    }
    
    // 创建小型内存池
    pool->small_pool = memory_pool_create(MEMORY_POOL_SMALL_SIZE, small_pool_count);
    if (!pool->small_pool) {
        LOGE("创建小型内存池失败");
        pthread_mutex_destroy(&pool->mutex);
        return -1;
    }
    
    // 创建中型内存池
    pool->medium_pool = memory_pool_create(MEMORY_POOL_MEDIUM_SIZE, medium_pool_count);
    if (!pool->medium_pool) {
        LOGE("创建中型内存池失败");
        memory_pool_destroy(pool->small_pool);
        pthread_mutex_destroy(&pool->mutex);
        return -1;
    }
    
    // 创建大型内存池
    pool->large_pool = memory_pool_create(MEMORY_POOL_LARGE_SIZE, large_pool_count);
    if (!pool->large_pool) {
        LOGE("创建大型内存池失败");
        memory_pool_destroy(pool->small_pool);
        memory_pool_destroy(pool->medium_pool);
        pthread_mutex_destroy(&pool->mutex);
        return -1;
    }
    
    // 初始化统计
    memset(&pool->stats, 0, sizeof(struct layered_memory_pool_stats));
    
    LOGI("初始化分层内存池完成: 小型池=%u, 中型池=%u, 大型池=%u", 
         small_pool_count, medium_pool_count, large_pool_count);
    
    return 0;
}

// 销毁分层内存池
void layered_memory_pool_destroy(struct layered_memory_pool* pool) {
    if (!pool) {
        return;
    }
    
    // 销毁所有内存池
    if (pool->small_pool) {
        memory_pool_destroy(pool->small_pool);
    }
    
    if (pool->medium_pool) {
        memory_pool_destroy(pool->medium_pool);
    }
    
    if (pool->large_pool) {
        memory_pool_destroy(pool->large_pool);
    }
    
    // 销毁互斥锁
    pthread_mutex_destroy(&pool->mutex);
    
    LOGI("销毁分层内存池完成");
}

// 从分层内存池分配内存
void* layered_memory_pool_alloc(struct layered_memory_pool* pool, size_t size) {
    if (!pool) {
        LOGE("分层内存池不能为空");
        return NULL;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    void* block = NULL;
    
    // 根据大小选择合适的内存池
    if (size <= MEMORY_POOL_SMALL_SIZE) {
        block = memory_pool_alloc(pool->small_pool);
        if (block) {
            pool->stats.small_alloc_count++;
        }
    } else if (size <= MEMORY_POOL_MEDIUM_SIZE) {
        block = memory_pool_alloc(pool->medium_pool);
        if (block) {
            pool->stats.medium_alloc_count++;
        }
    } else if (size <= MEMORY_POOL_LARGE_SIZE) {
        block = memory_pool_alloc(pool->large_pool);
        if (block) {
            pool->stats.large_alloc_count++;
        }
    } else {
        LOGE("请求大小超出分层内存池范围: %zu", size);
    }
    
    if (block) {
        pool->stats.total_alloc_count++;
    } else {
        pool->stats.failed_alloc_count++;
    }
    
    pthread_mutex_unlock(&pool->mutex);
    
    return block;
}

// 释放内存到分层内存池
void layered_memory_pool_free(struct layered_memory_pool* pool, void* block, size_t size) {
    if (!pool || !block) {
        LOGE("分层内存池或块不能为空");
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    // 根据大小选择合适的内存池
    if (size <= MEMORY_POOL_SMALL_SIZE) {
        memory_pool_free(pool->small_pool, block);
        pool->stats.small_free_count++;
    } else if (size <= MEMORY_POOL_MEDIUM_SIZE) {
        memory_pool_free(pool->medium_pool, block);
        pool->stats.medium_free_count++;
    } else if (size <= MEMORY_POOL_LARGE_SIZE) {
        memory_pool_free(pool->large_pool, block);
        pool->stats.large_free_count++;
    } else {
        LOGE("请求大小超出分层内存池范围: %zu", size);
    }
    
    pool->stats.total_free_count++;
    
    pthread_mutex_unlock(&pool->mutex);
}

// 获取分层内存池统计
void layered_memory_pool_get_stats(struct layered_memory_pool* pool, 
                                  struct layered_memory_pool_stats* stats) {
    if (!pool || !stats) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    *stats = pool->stats;
    
    // 获取各内存池的统计信息
    memory_pool_get_stats(pool->small_pool, &stats->small_stats);
    memory_pool_get_stats(pool->medium_pool, &stats->medium_stats);
    memory_pool_get_stats(pool->large_pool, &stats->large_stats);
    
    pthread_mutex_unlock(&pool->mutex);
}

// ==================== 高效内存分配实现 ====================

// 高效内存分配
void* resource_allocate_fast(size_t size) {
    // 使用线程本地内存池进行快速分配
    return thread_local_memory_pool_alloc(size);
}

// 高效内存释放
void resource_free_fast(void* ptr, size_t size) {
    // 使用线程本地内存池进行快速释放
    thread_local_memory_pool_free(ptr, size);
}