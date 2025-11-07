#ifndef COMPOSITOR_RESOURCE_H
#define COMPOSITOR_RESOURCE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 资源类型
typedef enum {
    RESOURCE_TYPE_TEXTURE,   // 纹理
    RESOURCE_TYPE_BUFFER,    // 缓冲区
    RESOURCE_TYPE_SHADER,    // 着色器
    RESOURCE_TYPE_PIPELINE,  // 渲染管线
    RESOURCE_TYPE_MEMORY      // 内存
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
};

// 内存池
struct memory_pool {
    void* memory;            // 内存块
    size_t size;             // 内存块大小
    size_t used;             // 已使用大小
    struct memory_pool* next; // 下一个内存池
};

// 资源管理器统计
struct resource_manager_stats {
    uint32_t total_resources;    // 总资源数
    uint32_t loaded_resources;   // 已加载资源数
    uint32_t error_resources;    // 错误资源数
    size_t total_memory;         // 总内存使用
    size_t peak_memory;          // 峰值内存使用
    uint32_t memory_pools;       // 内存池数量
    uint32_t gc_count;           // 垃圾回收次数
    uint64_t gc_time;            // 垃圾回收总时间
};

// 资源管理器
struct resource_manager {
    struct resource* resources[RESOURCE_TYPE_COUNT]; // 资源列表
    struct memory_pool* memory_pools; // 内存池列表
    uint32_t next_resource_id;  // 下一个资源ID
    size_t memory_limit;        // 内存限制
    size_t gc_threshold;        // 垃圾回收阈值
    uint64_t current_time;      // 当前时间
    struct resource_manager_stats stats; // 统计信息
    bool auto_gc_enabled;        // 是否启用自动垃圾回收
};

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

// 卸载资源
void resource_unload(struct resource* resource);

// 增加资源引用
void resource_add_ref(struct resource* resource);

// 减少资源引用
void resource_release(struct resource* resource);

// 更新资源使用统计
void resource_update_usage(struct resource* resource);

// 分配内存
void* resource_allocate(size_t size);

// 释放内存
void resource_free(void* ptr);

// 创建内存池
struct memory_pool* resource_create_memory_pool(size_t size);

// 销毁内存池
void resource_destroy_memory_pool(struct memory_pool* pool);

// 执行垃圾回收
int resource_gc(void);

// 设置内存限制
void resource_set_memory_limit(size_t limit);

// 设置垃圾回收阈值
void resource_set_gc_threshold(size_t threshold);

// 启用/禁用自动垃圾回收
void resource_set_auto_gc_enabled(bool enabled);

// 获取资源管理器统计
void resource_get_stats(struct resource_manager_stats* stats);

// 重置资源管理器统计
void resource_reset_stats(void);

// 更新资源管理器（每帧调用）
void resource_manager_update(void);

// 打印资源使用情况
void resource_print_usage(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_RESOURCE_H