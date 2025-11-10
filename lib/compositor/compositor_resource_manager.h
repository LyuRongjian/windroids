#ifndef COMPOSITOR_RESOURCE_MANAGER_H
#define COMPOSITOR_RESOURCE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// 资源类型
typedef enum {
    RESOURCE_TYPE_TEXTURE,   // 纹理
    RESOURCE_TYPE_BUFFER,    // 缓冲区
    RESOURCE_TYPE_SHADER,    // 着色器
    RESOURCE_TYPE_PIPELINE,  // 渲染管线
    RESOURCE_TYPE_MEMORY,     // 内存
    RESOURCE_TYPE_COUNT      // 资源类型数量
} resource_type_t;

// 资源状态
typedef enum {
    RESOURCE_STATE_UNLOADED, // 未加载
    RESOURCE_STATE_LOADING,  // 加载中
    RESOURCE_STATE_LOADED,   // 已加载
    RESOURCE_STATE_ERROR     // 错误
} resource_state_t;

// 资源使用统计
struct resource_usage {
    uint32_t ref_count;      // 引用计数
    uint64_t last_used;      // 最后使用时间
    uint32_t use_count;      // 使用次数
    uint64_t total_time;     // 总使用时间
};

// 资源
struct resource {
    uint32_t id;             // 资源ID
    resource_type_t type;    // 资源类型
    resource_state_t state;  // 资源状态
    char name[64];           // 资源名称
    size_t size;             // 资源大小
    void* data;              // 资源数据
    struct resource_usage usage; // 使用统计
    struct resource* next;   // 下一个资源（链表）
    bool async_loading;      // 是否异步加载
    bool high_priority;      // 是否高优先级
    uint32_t load_progress;  // 加载进度 (0-100)
};

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

// 异步加载线程池
struct async_load_thread_pool {
    pthread_t* threads;                   // 线程数组
    uint32_t thread_count;                // 线程数量
    bool shutdown;                        // 是否关闭线程池
    pthread_mutex_t queue_mutex;          // 队列互斥锁
    pthread_cond_t queue_cond;            // 队列条件变量
    pthread_cond_t finished_cond;         // 完成条件变量
};

// 资源加载回调函数类型
typedef void (*resource_load_callback_t)(struct resource* resource, bool success, void* user_data);

// 异步加载任务
struct async_load_task {
    struct resource* resource;           // 要加载的资源
    resource_load_callback_t callback;    // 加载完成回调
    void* user_data;                      // 用户数据
    struct async_load_task* next;         // 下一个任务
};

// 预加载资源描述符
struct preload_descriptor {
    char name[64];              // 资源名称
    resource_type_t type;       // 资源类型
    size_t size;                // 资源大小
    uint32_t priority;          // 预加载优先级 (0-10, 10为最高)
    bool critical;              // 是否为关键资源(必须在启动时加载)
    bool deferred;              // 是否延迟加载(在后台线程中加载)
    struct preload_descriptor* next; // 下一个描述符
};

// 预加载管理器
struct preload_manager {
    struct preload_descriptor* descriptors; // 预加载描述符列表
    uint32_t descriptor_count;              // 描述符数量
    uint32_t critical_loaded;               // 已加载的关键资源数
    uint32_t total_loaded;                  // 已加载的总资源数
    uint32_t total_size;                    // 总预加载大小
    uint32_t loaded_size;                   // 已加载大小
    uint64_t start_time;                    // 预加载开始时间
    uint64_t critical_time;                 // 关键资源加载完成时间
    uint64_t total_time;                    // 全部预加载完成时间
    bool enabled;                            // 是否启用预加载
    bool in_progress;                       // 是否正在进行预加载
    uint32_t max_concurrent_loads;          // 最大并发加载数
    uint32_t current_loads;                 // 当前加载数
};

// 纹理缓存项
struct texture_cache_item {
    uint32_t id;                // 纹理ID
    char name[64];              // 纹理名称
    void* texture_data;         // 纹理数据
    uint32_t width;             // 宽度
    uint32_t height;            // 高度
    uint32_t format;            // 格式
    size_t size;                // 纹理大小(字节)
    uint32_t mip_levels;        // Mipmap级别数
    bool compressed;            // 是否压缩
    uint32_t ref_count;         // 引用计数
    uint64_t last_used;         // 最后使用时间
    uint32_t use_count;         // 使用次数
    bool is_pinned;             // 是否固定(不会被淘汰)
    uint8_t priority;           // 优先级 (0-10, 10为最高)
    uint32_t memory_type;       // 内存类型 (0=系统内存, 1=GPU内存)
    struct texture_cache_item* next; // 下一个缓存项
    struct texture_cache_item* prev; // 前一个缓存项
};

// 纹理缓存统计
struct texture_cache_stats {
    uint32_t total_items;       // 总缓存项数
    uint32_t active_items;      // 活跃缓存项数
    uint32_t pinned_items;      // 固定缓存项数
    uint32_t compressed_items;  // 压缩缓存项数
    size_t total_memory;        // 总内存使用
    size_t gpu_memory;          // GPU内存使用
    size_t system_memory;       // 系统内存使用
    size_t compressed_memory;    // 压缩纹理内存
    uint32_t evictions;         // 淘汰次数
    uint32_t hits;              // 缓存命中次数
    uint32_t misses;            // 缓存未命中次数
    float hit_ratio;            // 命中率
    uint64_t last_eviction_time; // 最后淘汰时间
    uint32_t compression_ratio;  // 压缩比 (百分比)
    uint64_t total_load_time;    // 总加载时间
    uint64_t avg_load_time;      // 平均加载时间
};

// 纹理缓存管理器
struct texture_cache_manager {
    struct texture_cache_item* items; // 缓存项列表(哈希表)
    struct texture_cache_item* lru_list; // LRU列表头
    struct texture_cache_item* mru_list; // MRU列表头
    uint32_t capacity;          // 缓存容量(MB)
    uint32_t current_size;      // 当前使用大小(MB)
    uint32_t max_items;         // 最大缓存项数
    uint32_t item_count;        // 当前缓存项数
    uint32_t hash_size;         // 哈希表大小
    bool compression_enabled;   // 是否启用压缩
    bool streaming_enabled;      // 是否启用流式加载
    uint32_t compression_level; // 压缩级别 (0-9)
    uint32_t eviction_threshold; // 淘汰阈值(百分比)
    uint32_t lru_update_interval; // LRU更新间隔(帧)
    uint32_t frame_counter;     // 帧计数器
    struct texture_cache_stats stats; // 统计信息
};

// 资源管理器统计
struct resource_manager_stats {
    uint32_t total_resources;    // 总资源数
    uint32_t loaded_resources;   // 已加载资源数
    uint32_t error_resources;    // 错误资源数
    size_t total_memory;         // 总内存使用
    size_t used_memory;         // 已使用内存
    size_t free_memory;         // 空闲内存
    size_t peak_memory;          // 峰值内存使用
    uint32_t allocation_count;  // 分配次数
    uint32_t free_count;        // 释放次数
};

// 资源管理器
struct resource_manager {
    struct resource* resources[RESOURCE_TYPE_COUNT]; // 资源列表
    uint32_t next_resource_id;  // 下一个资源ID
    size_t memory_limit;        // 内存限制
    uint64_t current_time;      // 当前时间
    struct resource_manager_stats stats; // 统计信息
    struct preload_manager preload_mgr; // 预加载管理器
    struct texture_cache_manager texture_cache_mgr; // 纹理缓存管理器
    struct async_load_queue async_queue; // 异步加载队列
    struct async_load_thread_pool thread_pool; // 异步加载线程池
    struct async_load_task* pending_tasks; // 待处理任务队列
    pthread_mutex_t pending_tasks_mutex; // 待处理任务队列互斥锁
};

// ==================== 基础资源管理API ====================

// 初始化资源管理器
int resource_manager_init(size_t memory_limit);

// 销毁资源管理器
void resource_manager_destroy(void);

// 创建资源
struct resource* resource_create(resource_type_t type, const char* name, size_t size);

// 销毁资源
void resource_destroy(struct resource* resource);

// 查找资源
struct resource* resource_find(uint32_t id);
struct resource* resource_find_by_name(const char* name);

// 加载资源
int resource_load(struct resource* resource);

// 异步加载资源
int resource_load_async(struct resource* resource, bool high_priority);
int resource_load_async_with_callback(struct resource* resource, bool high_priority,
                                     resource_load_callback_t callback, void* user_data);

// 卸载资源
void resource_unload(struct resource* resource);

// 获取资源加载进度
uint32_t resource_get_load_progress(struct resource* resource);

// 取消异步加载
void resource_cancel_async_load(struct resource* resource);

// 处理异步加载队列
void resource_process_async_loads(void);

// 增加资源引用
void resource_add_ref(struct resource* resource);

// 减少资源引用
void resource_release(struct resource* resource);

// 更新资源使用统计
void resource_update_usage(struct resource* resource);

// 设置内存限制
void resource_set_memory_limit(size_t limit);

// 获取资源管理器统计
void resource_get_stats(struct resource_manager_stats* stats);

// 重置资源管理器统计
void resource_reset_stats(void);

// 更新资源管理器（每帧调用）
void resource_manager_update(void);

// 打印资源使用情况
void resource_print_usage(void);

// ==================== 异步加载API ====================

// 线程池管理
int async_load_thread_pool_init(uint32_t thread_count);
void async_load_thread_pool_shutdown(void);
void* async_load_thread_func(void* arg);

// 任务队列管理
int async_load_task_push(struct async_load_task* task);
struct async_load_task* async_load_task_pop(void);
void async_load_task_execute(struct async_load_task* task);

// ==================== 预加载管理API ====================

// 预加载管理函数
void resource_preload_init(void);
void resource_preload_shutdown(void);
int resource_preload_add(const char* name, resource_type_t type, size_t size, 
                       uint32_t priority, bool critical, bool deferred);
int resource_preload_remove(const char* name);
int resource_preload_start(void);
int resource_preload_update(void); // 每帧调用，处理预加载进度
void resource_preload_cancel(void);
void resource_preload_get_stats(uint32_t* critical_loaded, uint32_t* total_loaded, 
                                uint32_t* total_size, uint32_t* loaded_size,
                                uint64_t* critical_time, uint64_t* total_time);
bool resource_preload_is_in_progress(void);
void resource_preload_set_enabled(bool enabled);
void resource_preload_set_max_concurrent_loads(uint32_t max_loads);

// ==================== 纹理缓存API ====================

// 纹理缓存管理函数
int texture_cache_init(uint32_t capacity_mb, uint32_t max_items, uint32_t hash_size);
void texture_cache_shutdown(void);
void* texture_cache_get(uint32_t id);
void* texture_cache_get_by_name(const char* name);
int texture_cache_add(uint32_t id, const char* name, void* texture_data, 
                     uint32_t width, uint32_t height, uint32_t format, 
                     size_t size, uint32_t mip_levels, bool compressed, 
                     uint8_t priority, uint32_t memory_type);
int texture_cache_remove(uint32_t id);
int texture_cache_remove_by_name(const char* name);
void texture_cache_pin(uint32_t id);
void texture_cache_unpin(uint32_t id);
void texture_cache_set_priority(uint32_t id, uint8_t priority);
void texture_cache_update_usage(uint32_t id);
void texture_cache_compress(uint32_t id);
void texture_cache_decompress(uint32_t id);
void texture_cache_evict(uint32_t count);
void texture_cache_evict_by_priority(uint8_t min_priority);
void texture_cache_evict_by_size(size_t target_size);
void texture_cache_clear(void);
void texture_cache_get_stats(struct texture_cache_stats* stats);
void texture_cache_reset_stats(void);
void texture_cache_set_compression_enabled(bool enabled);
void texture_cache_set_compression_level(uint32_t level);
void texture_cache_set_streaming_enabled(bool enabled);
void texture_cache_set_capacity(uint32_t capacity_mb);
void texture_cache_set_eviction_threshold(uint32_t threshold);
void texture_cache_update(void); // 每帧调用，更新LRU和统计信息
void texture_cache_print_stats(void);

// ==================== 工具函数 ====================

// 获取当前时间（毫秒）
uint64_t resource_get_time(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_RESOURCE_MANAGER_H