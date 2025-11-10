#ifndef COMPOSITOR_VULKAN_H
#define COMPOSITOR_VULKAN_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "vulkan.h"
#include "vulkan_android.h"
#include "android/native_window.h"

#ifdef __cplusplus
extern "C" {
#endif

// 命令缓冲区类型
typedef enum {
    COMMAND_BUFFER_TYPE_PRIMARY = 0,  // 主要命令缓冲区，每帧使用
    COMMAND_BUFFER_TYPE_SECONDARY,     // 次要命令缓冲区，用于预记录内容
    COMMAND_BUFFER_TYPE_COUNT
} command_buffer_type_t;

// 命令缓冲区缓存项
struct command_buffer_cache {
    VkCommandBuffer command_buffer;
    bool is_recorded;
    bool is_dirty;
    uint64_t last_used_frame;
};

// 渲染批次
struct render_batch {
    uint32_t vertex_count;
    uint32_t index_count;
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    VkDescriptorSet descriptor_set;
    VkPipeline pipeline;
    uint32_t texture_id;
    bool is_transparent;
};

// 批处理管理器
struct batch_manager {
    struct render_batch* batches;
    uint32_t batch_count;
    uint32_t max_batches;
    uint32_t current_batch;
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    void* vertex_mapped;
    void* index_mapped;
    uint32_t vertex_capacity;
    uint32_t index_capacity;
    uint32_t vertex_used;
    uint32_t index_used;
    bool initialized;
};

// 渲染任务函数类型
typedef void (*render_task_func_t)(VkCommandBuffer, void*);

// 渲染线程数据
struct render_thread_data {
    pthread_t thread_id;
    uint32_t thread_index;
    struct vulkan_state* vk;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore semaphore;
    VkFence fence;
    bool should_exit;
    bool is_ready;
    bool active;
    bool has_work;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    struct {
        void (*render_func)(VkCommandBuffer, void*);
        void* user_data;
        uint32_t task_id;
        uint32_t start_task;
        uint32_t end_task;
    } task;
};

// 多线程渲染管理器
struct multithread_renderer {
    struct render_thread_data* threads;
    uint32_t thread_count;
    bool enabled;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool has_work;
    bool work_done;
    bool initialized;
    VkSemaphore render_complete_semaphore;
};

// Android特定优化
struct android_optimizations {
    bool use_external_memory;        // 使用Android外部内存
    bool use_queue_family_foreign;   // 使用外部队列族
    bool use_android_hardware_buffer; // 使用Android硬件缓冲区
    bool use_async_compute;          // 使用异步计算
    bool use_surface_rotation;       // 使用表面旋转优化
    uint32_t surface_transform;      // 表面变换标志
    bool use_protected_content;      // 使用受保护内容
    bool use_android_sync;           // 使用Android同步
    uint32_t performance_level;      // 性能级别 (0-3)
    bool use_low_latency_mode;       // 使用低延迟模式
    bool use_power_saving_mode;      // 使用省电模式
    float thermal_throttling_factor; // 热节流因子
};

// Vulkan状态结构
struct vulkan_state {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkSurfaceKHR surface;
    VkDevice device;
    VkQueue queue;
    VkSwapchainKHR swapchain;
    VkImage* images;
    VkImageView* image_views;
    VkFramebuffer* framebuffers;
    VkRenderPass render_pass;
    VkCommandPool command_pool;
    VkCommandBuffer* command_buffers;
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkFence* in_flight_fences;
    uint32_t image_count;
    uint32_t current_image;
    uint32_t queue_family_index;
    bool initialized;
    int width, height;
    
    // 命令缓冲区缓存
    struct command_buffer_cache* primary_cache;
    struct command_buffer_cache* secondary_cache;
    uint32_t secondary_buffer_count;
    uint64_t current_frame;
    bool use_command_buffer_cache;
    
    // 批处理管理器
    struct batch_manager batch_manager;
    
    // 多线程渲染管理器
    struct multithread_renderer multithread_renderer;
    
    // Android特定优化
    struct android_optimizations android_opts;
};

// 初始化Vulkan
int vulkan_init(struct vulkan_state* vk, ANativeWindow* window, int width, int height);

// 销毁Vulkan
void vulkan_destroy(struct vulkan_state* vk);

// 重建交换链
int vulkan_recreate_swapchain(struct vulkan_state* vk, int width, int height);

// 开始渲染帧
int vulkan_begin_frame(struct vulkan_state* vk, uint32_t* image_index);

// 结束渲染帧
int vulkan_end_frame(struct vulkan_state* vk, uint32_t image_index);

// 获取当前帧缓冲区
VkFramebuffer vulkan_get_current_framebuffer(struct vulkan_state* vk, uint32_t image_index);

// 获取命令缓冲区
VkCommandBuffer vulkan_get_command_buffer(struct vulkan_state* vk, uint32_t image_index);

// 初始化命令缓冲区缓存
int vulkan_init_command_buffer_cache(struct vulkan_state* vk, uint32_t secondary_buffer_count);

// 销毁命令缓冲区缓存
void vulkan_destroy_command_buffer_cache(struct vulkan_state* vk);

// 获取缓存的命令缓冲区
VkCommandBuffer vulkan_get_cached_command_buffer(struct vulkan_state* vk, command_buffer_type_t type, uint32_t index);

// 标记命令缓冲区为脏
void vulkan_mark_command_buffer_dirty(struct vulkan_state* vk, command_buffer_type_t type, uint32_t index);

// 记录次要命令缓冲区
int vulkan_record_secondary_command_buffer(struct vulkan_state* vk, uint32_t index, 
                                           void (*record_func)(VkCommandBuffer, void*), void* user_data);

// 执行缓存的命令缓冲区
void vulkan_execute_cached_command_buffers(struct vulkan_state* vk, VkCommandBuffer primary_buffer);

// 更新帧计数
void vulkan_update_frame_counter(struct vulkan_state* vk);

// 批处理相关函数
int vulkan_init_batch_manager(struct vulkan_state* vk, uint32_t max_batches, 
                              uint32_t vertex_capacity, uint32_t index_capacity);
void vulkan_destroy_batch_manager(struct vulkan_state* vk);
int vulkan_begin_batch(struct vulkan_state* vk, VkPipeline pipeline, VkDescriptorSet descriptor_set, 
                       uint32_t texture_id, bool is_transparent);
int vulkan_add_to_batch(struct vulkan_state* vk, const void* vertices, uint32_t vertex_count,
                        const uint32_t* indices, uint32_t index_count);
int vulkan_end_batch(struct vulkan_state* vk);
int vulkan_render_batches(struct vulkan_state* vk, VkCommandBuffer command_buffer);

// 多线程渲染相关函数
int vulkan_init_multithread_renderer(struct vulkan_state* vk, uint32_t thread_count);
void vulkan_destroy_multithread_renderer(struct vulkan_state* vk);
int vulkan_render_multithread(struct vulkan_state* vk, render_task_func_t render_func, void* user_data, 
                              uint32_t task_count);
static void* render_thread_func(void* arg);

// Android优化相关函数
int vulkan_init_android_optimizations(struct vulkan_state* vk);
void vulkan_destroy_android_optimizations(struct vulkan_state* vk);
int vulkan_apply_android_optimizations(struct vulkan_state* vk);
int vulkan_update_android_performance_level(struct vulkan_state* vk, uint32_t level);
int vulkan_set_android_surface_transform(struct vulkan_state* vk, uint32_t transform);
int vulkan_enable_android_hardware_buffer(struct vulkan_state* vk, bool enable);
int vulkan_enable_android_external_memory(struct vulkan_state* vk, bool enable);
int vulkan_update_thermal_throttling(struct vulkan_state* vk, float factor);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_VULKAN_H