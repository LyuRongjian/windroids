#ifndef COMPOSITOR_MEMORY_POOL_OPT_H
#define COMPOSITOR_MEMORY_POOL_OPT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// 内存池块大小等级
#define MEMORY_POOL_BLOCK_SIZE_32    32      // 32字节块
#define MEMORY_POOL_BLOCK_SIZE_64    64      // 64字节块
#define MEMORY_POOL_BLOCK_SIZE_128   128     // 128字节块
#define MEMORY_POOL_BLOCK_SIZE_256   256     // 256字节块
#define MEMORY_POOL_BLOCK_SIZE_512   512     // 512字节块
#define MEMORY_POOL_BLOCK_SIZE_1024  1024    // 1KB块
#define MEMORY_POOL_BLOCK_SIZE_2048  2048    // 2KB块
#define MEMORY_POOL_BLOCK_SIZE_4096  4096    // 4KB块
#define MEMORY_POOL_BLOCK_SIZE_8192  8192    // 8KB块
#define MEMORY_POOL_BLOCK_SIZE_16384 16384   // 16KB块
#define MEMORY_POOL_BLOCK_SIZE_COUNT 11      // 块大小等级数

// 内存池块大小数组
static const size_t g_memory_pool_block_sizes[MEMORY_POOL_BLOCK_SIZE_COUNT] = {
    MEMORY_POOL_BLOCK_SIZE_32,
    MEMORY_POOL_BLOCK_SIZE_64,
    MEMORY_POOL_BLOCK_SIZE_128,
    MEMORY_POOL_BLOCK_SIZE_256,
    MEMORY_POOL_BLOCK_SIZE_512,
    MEMORY_POOL_BLOCK_SIZE_1024,
    MEMORY_POOL_BLOCK_SIZE_2048,
    MEMORY_POOL_BLOCK_SIZE_4096,
    MEMORY_POOL_BLOCK_SIZE_8192,
    MEMORY_POOL_BLOCK_SIZE_16384,
    0  // 终止符
};

// 内存池块头部
struct memory_pool_block_header {
    struct memory_pool_block_header* next;  // 下一个空闲块
    uint32_t magic;                         // 魔数，用于检测损坏
    uint32_t size_class;                    // 大小类别
    uint64_t alloc_time;                    // 分配时间
    uint32_t pool_id;                       // 所属池ID
};

// 内存池统计
struct memory_pool_opt_stats {
    uint32_t total_allocations;             // 总分配次数
    uint32_t total_frees;                   // 总释放次数
    uint32_t current_allocations;           // 当前分配数
    size_t total_allocated_bytes;           // 总分配字节数
    size_t current_allocated_bytes;         // 当前分配字节数
    size_t peak_allocated_bytes;            // 峰值分配字节数
    uint32_t cache_hits;                    // 缓存命中次数
    uint32_t cache_misses;                  // 缓存未命中次数
    float fragmentation_ratio;              // 碎片化比率
    float utilization_ratio;               // 利用率
    uint64_t avg_alloc_time_ns;             // 平均分配时间(纳秒)
    uint64_t avg_free_time_ns;              // 平均释放时间(纳秒)
    uint32_t lock_contentions;              // 锁竞争次数
    uint32_t pool_expansions;               // 池扩展次数
    uint32_t pool_shrinks;                  // 池收缩次数
};

// 内存池配置
struct memory_pool_opt_config {
    uint32_t initial_block_counts[MEMORY_POOL_BLOCK_SIZE_COUNT]; // 初始块数量
    uint32_t max_block_counts[MEMORY_POOL_BLOCK_SIZE_COUNT];     // 最大块数量
    uint32_t growth_factor;                                        // 增长因子(百分比)
    uint32_t shrink_threshold;                                     // 收缩阈值(百分比)
    uint32_t auto_resize_interval_ms;                              // 自动调整间隔(毫秒)
    bool enable_thread_cache;                                      // 启用线程缓存
    bool enable_prefetch;                                          // 启用预取
    bool enable_lock_free;                                         // 启用无锁操作
    bool enable_statistics;                                        // 启用统计
    bool enable_profiling;                                         // 启用性能分析
};

// 内存池
struct memory_pool_opt {
    void* memory;                                                  // 内存池起始地址
    size_t total_size;                                             // 总大小
    size_t used_size;                                              // 已使用大小
    struct memory_pool_block_header* free_lists[MEMORY_POOL_BLOCK_SIZE_COUNT]; // 各大小等级的空闲列表
    uint32_t block_counts[MEMORY_POOL_BLOCK_SIZE_COUNT];           // 各大小等级的块数量
    uint32_t used_block_counts[MEMORY_POOL_BLOCK_SIZE_COUNT];     // 各大小等级的已使用块数量
    uint32_t max_block_counts[MEMORY_POOL_BLOCK_SIZE_COUNT];       // 各大小等级的最大块数量
    struct memory_pool_opt_stats stats;                            // 统计信息
    struct memory_pool_opt_config config;                          // 配置
    pthread_mutex_t mutex;                                         // 互斥锁
    pthread_spinlock_t spinlocks[MEMORY_POOL_BLOCK_SIZE_COUNT];    // 各大小等级的自旋锁
    uint64_t last_resize_time;                                     // 最后调整时间
    uint32_t pool_id;                                              // 池ID
    bool initialized;                                               // 是否已初始化
};

// 线程本地缓存
struct memory_pool_thread_cache {
    struct memory_pool_block_header* free_lists[MEMORY_POOL_BLOCK_SIZE_COUNT]; // 各大小等级的空闲列表
    uint32_t cache_counts[MEMORY_POOL_BLOCK_SIZE_COUNT];               // 各大小等级的缓存数量
    uint32_t max_cache_counts[MEMORY_POOL_BLOCK_SIZE_COUNT];           // 各大小等级的最大缓存数量
    uint64_t last_flush_time;                                         // 最后刷新时间
    uint32_t thread_id;                                               // 线程ID
    bool initialized;                                                 // 是否已初始化
};

// 内存池管理器
struct memory_pool_opt_manager {
    struct memory_pool_opt* pools;                                   // 内存池数组
    uint32_t pool_count;                                              // 内存池数量
    uint32_t max_pools;                                               // 最大内存池数量
    struct memory_pool_opt_config default_config;                    // 默认配置
    struct memory_pool_opt_stats global_stats;                       // 全局统计
    pthread_mutex_t manager_mutex;                                   // 管理器互斥锁
    pthread_key_t thread_cache_key;                                  // 线程缓存键
    bool initialized;                                                 // 是否已初始化
};

// ==================== 内存池管理API ====================

// 初始化内存池管理器
int memory_pool_opt_manager_init(uint32_t max_pools);

// 销毁内存池管理器
void memory_pool_opt_manager_destroy(void);

// 创建内存池
struct memory_pool_opt* memory_pool_opt_create(uint32_t pool_id, 
                                              const struct memory_pool_opt_config* config);

// 销毁内存池
void memory_pool_opt_destroy(struct memory_pool_opt* pool);

// 从内存池分配内存
void* memory_pool_opt_alloc(struct memory_pool_opt* pool, size_t size);

// 释放内存到内存池
void memory_pool_opt_free(struct memory_pool_opt* pool, void* ptr);

// 重新分配内存
void* memory_pool_opt_realloc(struct memory_pool_opt* pool, void* ptr, size_t new_size);

// 获取内存块大小
size_t memory_pool_opt_get_size(void* ptr);

// 检查内存是否来自内存池
bool memory_pool_opt_is_from_pool(void* ptr);

// 内存池压缩
int memory_pool_opt_compact(struct memory_pool_opt* pool);

// 内存池碎片整理
int memory_pool_opt_defragment(struct memory_pool_opt* pool);

// 获取内存池统计
void memory_pool_opt_get_stats(struct memory_pool_opt* pool, 
                               struct memory_pool_opt_stats* stats);

// 重置内存池统计
void memory_pool_opt_reset_stats(struct memory_pool_opt* pool);

// 打印内存池统计
void memory_pool_opt_print_stats(struct memory_pool_opt* pool);

// 设置内存池配置
int memory_pool_opt_set_config(struct memory_pool_opt* pool, 
                               const struct memory_pool_opt_config* config);

// 获取内存池配置
void memory_pool_opt_get_config(struct memory_pool_opt* pool, 
                               struct memory_pool_opt_config* config);

// ==================== 线程本地缓存API ====================

// 初始化线程本地缓存
int memory_pool_opt_thread_cache_init(void);

// 销毁线程本地缓存
void memory_pool_opt_thread_cache_destroy(void);

// 从线程本地缓存分配内存
void* memory_pool_opt_thread_cache_alloc(size_t size);

// 释放内存到线程本地缓存
void memory_pool_opt_thread_cache_free(void* ptr);

// 刷新线程本地缓存
void memory_pool_opt_thread_cache_flush(void);

// 获取线程本地缓存统计
void memory_pool_opt_thread_cache_get_stats(struct memory_pool_opt_stats* stats);

// ==================== 内存池管理器API ====================

// 获取内存池
struct memory_pool_opt* memory_pool_opt_manager_get_pool(uint32_t pool_id);

// 添加内存池到管理器
int memory_pool_opt_manager_add_pool(struct memory_pool_opt* pool);

// 从管理器移除内存池
int memory_pool_opt_manager_remove_pool(uint32_t pool_id);

// 获取管理器统计
void memory_pool_opt_manager_get_stats(struct memory_pool_opt_stats* stats);

// 重置管理器统计
void memory_pool_opt_manager_reset_stats(void);

// 打印管理器统计
void memory_pool_opt_manager_print_stats(void);

// ==================== 便利API ====================

// 快速分配内存
void* memory_pool_opt_alloc_fast(size_t size);

// 快速释放内存
void memory_pool_opt_free_fast(void* ptr);

// 快速重新分配内存
void* memory_pool_opt_realloc_fast(void* ptr, size_t new_size);

// 获取默认内存池
struct memory_pool_opt* memory_pool_opt_get_default(void);

// 设置默认内存池
int memory_pool_opt_set_default(uint32_t pool_id);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_MEMORY_POOL_OPT_H