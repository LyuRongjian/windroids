#ifndef COMPOSITOR_MEMORY_POOL_H
#define COMPOSITOR_MEMORY_POOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// 内存池大小等级
#define MEMORY_POOL_SIZE_SMALL   64      // 小内存池大小
#define MEMORY_POOL_SIZE_MEDIUM  256     // 中等内存池大小
#define MEMORY_POOL_SIZE_LARGE   1024    // 大内存池大小
#define MEMORY_POOL_SIZE_XLARGE  4096    // 超大内存池大小
#define MEMORY_POOL_SIZE_LEVELS  4       // 内存池大小等级数

// 内存池统计
struct memory_pool_stats {
    uint32_t total_allocations;  // 总分配次数
    uint32_t total_frees;        // 总释放次数
    uint32_t current_allocations; // 当前分配数
    size_t total_allocated;      // 总分配内存
    size_t total_freed;          // 总释放内存
    size_t current_allocated;    // 当前分配内存
    size_t peak_allocated;       // 峰值分配内存
    size_t fragmentation;        // 碎片化程度
    float efficiency;            // 效率(已分配内存/总内存)
    uint32_t pool_hits;          // 池命中次数
    uint32_t pool_misses;        // 池未命中次数
    float hit_ratio;             // 命中率
    uint64_t avg_alloc_time;     // 平均分配时间
    uint64_t avg_free_time;      // 平均释放时间
};

// 内存池使用历史
struct memory_pool_history {
    uint64_t timestamp;          // 时间戳
    size_t allocated;            // 分配内存
    size_t freed;                // 释放内存
    size_t fragmentation;        // 碎片化程度
    float efficiency;            // 效率
    uint32_t allocation_count;   // 分配次数
    uint32_t free_count;          // 释放次数
};

// 内存块头部
struct memory_block_header {
    size_t size;                 // 块大小
    bool free;                   // 是否空闲
    struct memory_block_header* next; // 下一个块
    struct memory_block_header* prev; // 前一个块
    uint64_t timestamp;          // 分配时间戳
    uint32_t magic;              // 魔数(用于检测损坏)
};

// 线程本地内存池
struct thread_local_memory_pool {
    void* pools[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的内存池
    size_t pool_sizes[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的池大小
    uint32_t pool_counts[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的池数量
    struct memory_block_header* free_blocks[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的空闲块列表
    struct memory_pool_stats stats; // 统计信息
    pthread_key_t tls_key;       // 线程本地存储键
    bool initialized;             // 是否已初始化
};

// 内存池缓存
struct memory_pool_cache {
    void* cache[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的缓存
    size_t cache_sizes[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的缓存大小
    uint32_t cache_counts[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的缓存数量
    uint32_t max_cache_size;     // 最大缓存大小
    uint32_t current_cache_size; // 当前缓存大小
    uint64_t last_access_time;   // 最后访问时间
    uint32_t access_count;       // 访问次数
};

// 智能内存池
struct smart_memory_pool {
    void* memory;                // 内存池起始地址
    size_t size;                 // 内存池大小
    size_t used;                 // 已使用大小
    struct memory_block_header* blocks; // 内存块列表
    struct memory_pool_stats stats; // 统计信息
    struct memory_pool_history* history; // 使用历史
    uint32_t history_count;      // 历史记录数量
    uint32_t history_capacity;   // 历史记录容量
    struct thread_local_memory_pool* thread_pools; // 线程本地内存池
    uint32_t thread_pool_count;  // 线程池数量
    struct memory_pool_cache cache; // 内存池缓存
    pthread_mutex_t mutex;       // 互斥锁
    bool auto_compact;           // 自动压缩
    uint32_t compact_threshold;  // 压缩阈值
    uint32_t compact_interval;   // 压缩间隔
    uint64_t last_compact_time;  // 最后压缩时间
    uint32_t allocation_strategy; // 分配策略 (0=首次适应, 1=最佳适应, 2=最差适应)
    bool use_tls;                // 是否使用线程本地存储
    bool use_cache;              // 是否使用缓存
    bool track_history;          // 是否跟踪历史
};

// 内存池优化器
struct memory_pool_optimizer {
    struct smart_memory_pool* pool; // 要优化的内存池
    uint32_t optimization_interval; // 优化间隔
    uint64_t last_optimization_time; // 最后优化时间
    float fragmentation_threshold; // 碎片化阈值
    float efficiency_threshold;   // 效率阈值
    uint32_t max_optimization_time; // 最大优化时间
    bool auto_optimize;          // 自动优化
    uint32_t optimization_level; // 优化级别 (0=最低, 10=最高)
    struct memory_pool_stats baseline_stats; // 基线统计
    uint32_t optimization_count; // 优化次数
    uint64_t total_optimization_time; // 总优化时间
    uint64_t avg_optimization_time; // 平均优化时间
};

// 分层内存池
struct tiered_memory_pool {
    struct smart_memory_pool* pools[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的内存池
    size_t pool_sizes[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的池大小
    uint32_t pool_counts[MEMORY_POOL_SIZE_LEVELS]; // 各大小等级的池数量
    struct memory_pool_stats total_stats; // 总统计信息
    pthread_mutex_t mutex;       // 互斥锁
    bool auto_balance;           // 自动平衡
    uint32_t balance_interval;   // 平衡间隔
    uint64_t last_balance_time;  // 最后平衡时间
    float balance_threshold;     // 平衡阈值
};

// ==================== 内存池管理API ====================

// 初始化内存池
int memory_pool_init(size_t size);

// 销毁内存池
void memory_pool_destroy(void);

// 从内存池分配内存
void* memory_pool_alloc(size_t size);

// 释放内存到内存池
void memory_pool_free(void* ptr);

// 重新分配内存
void* memory_pool_realloc(void* ptr, size_t size);

// 获取内存块大小
size_t memory_pool_get_size(void* ptr);

// 检查内存是否来自内存池
bool memory_pool_is_from_pool(void* ptr);

// 内存池压缩
void memory_pool_compact(void);

// 内存池碎片整理
void memory_pool_defragment(void);

// 获取内存池统计
void memory_pool_get_stats(struct memory_pool_stats* stats);

// 重置内存池统计
void memory_pool_reset_stats(void);

// 打印内存池统计
void memory_pool_print_stats(void);

// ==================== 智能内存池API ====================

// 初始化智能内存池
int smart_memory_pool_init(size_t size, bool auto_compact, uint32_t compact_threshold,
                           uint32_t compact_interval, uint32_t allocation_strategy,
                           bool use_tls, bool use_cache, bool track_history);

// 销毁智能内存池
void smart_memory_pool_destroy(void);

// 从智能内存池分配内存
void* smart_memory_pool_alloc(size_t size);

// 释放内存到智能内存池
void smart_memory_pool_free(void* ptr);

// 重新分配内存
void* smart_memory_pool_realloc(void* ptr, size_t size);

// 智能内存池压缩
void smart_memory_pool_compact(void);

// 智能内存池碎片整理
void smart_memory_pool_defragment(void);

// 获取智能内存池统计
void smart_memory_pool_get_stats(struct memory_pool_stats* stats);

// 重置智能内存池统计
void smart_memory_pool_reset_stats(void);

// 打印智能内存池统计
void smart_memory_pool_print_stats(void);

// 设置自动压缩
void smart_memory_pool_set_auto_compact(bool enabled);

// 设置压缩阈值
void smart_memory_pool_set_compact_threshold(uint32_t threshold);

// 设置压缩间隔
void smart_memory_pool_set_compact_interval(uint32_t interval);

// 设置分配策略
void smart_memory_pool_set_allocation_strategy(uint32_t strategy);

// 启用/禁用线程本地存储
void smart_memory_pool_set_tls_enabled(bool enabled);

// 启用/禁用缓存
void smart_memory_pool_set_cache_enabled(bool enabled);

// 启用/禁用历史跟踪
void smart_memory_pool_set_history_tracking(bool enabled);

// 获取内存池使用历史
void smart_memory_pool_get_history(struct memory_pool_history* history, uint32_t* count);

// 清除内存池使用历史
void smart_memory_pool_clear_history(void);

// ==================== 线程本地内存池API ====================

// 初始化线程本地内存池
int thread_local_memory_pool_init(void);

// 销毁线程本地内存池
void thread_local_memory_pool_destroy(void);

// 从线程本地内存池分配内存
void* thread_local_memory_pool_alloc(size_t size);

// 释放内存到线程本地内存池
void thread_local_memory_pool_free(void* ptr);

// 获取线程本地内存池统计
void thread_local_memory_pool_get_stats(struct memory_pool_stats* stats);

// 重置线程本地内存池统计
void thread_local_memory_pool_reset_stats(void);

// 打印线程本地内存池统计
void thread_local_memory_pool_print_stats(void);

// ==================== 内存池优化器API ====================

// 初始化内存池优化器
int memory_pool_optimizer_init(struct smart_memory_pool* pool, uint32_t optimization_interval,
                              float fragmentation_threshold, float efficiency_threshold,
                              uint32_t max_optimization_time, bool auto_optimize,
                              uint32_t optimization_level);

// 销毁内存池优化器
void memory_pool_optimizer_destroy(void);

// 执行内存池优化
void memory_pool_optimizer_optimize(void);

// 设置优化间隔
void memory_pool_optimizer_set_interval(uint32_t interval);

// 设置碎片化阈值
void memory_pool_optimizer_set_fragmentation_threshold(float threshold);

// 设置效率阈值
void memory_pool_optimizer_set_efficiency_threshold(float threshold);

// 设置最大优化时间
void memory_pool_optimizer_set_max_optimization_time(uint32_t time);

// 启用/禁用自动优化
void memory_pool_optimizer_set_auto_optimize(bool enabled);

// 设置优化级别
void memory_pool_optimizer_set_level(uint32_t level);

// 获取优化器统计
void memory_pool_optimizer_get_stats(uint32_t* optimization_count, uint64_t* total_time,
                                    uint64_t* avg_time);

// 重置优化器统计
void memory_pool_optimizer_reset_stats(void);

// 打印优化器统计
void memory_pool_optimizer_print_stats(void);

// ==================== 分层内存池API ====================

// 初始化分层内存池
int tiered_memory_pool_init(size_t small_size, size_t medium_size, size_t large_size, size_t xlarge_size,
                           bool auto_balance, uint32_t balance_interval, float balance_threshold);

// 销毁分层内存池
void tiered_memory_pool_destroy(void);

// 从分层内存池分配内存
void* tiered_memory_pool_alloc(size_t size);

// 释放内存到分层内存池
void tiered_memory_pool_free(void* ptr);

// 分层内存池平衡
void tiered_memory_pool_balance(void);

// 获取分层内存池统计
void tiered_memory_pool_get_stats(struct memory_pool_stats* stats);

// 重置分层内存池统计
void tiered_memory_pool_reset_stats(void);

// 打印分层内存池统计
void tiered_memory_pool_print_stats(void);

// 设置自动平衡
void tiered_memory_pool_set_auto_balance(bool enabled);

// 设置平衡间隔
void tiered_memory_pool_set_balance_interval(uint32_t interval);

// 设置平衡阈值
void tiered_memory_pool_set_balance_threshold(float threshold);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_POOL_H