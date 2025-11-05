#ifndef COMPOSITOR_VULKAN_CORE_H
#define COMPOSITOR_VULKAN_CORE_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <stdbool.h>
#include <android/native_window.h>
#include <stdint.h>
#include "compositor.h"
#include "compositor_window.h"

// 前向声明
struct CompositorState;
typedef struct CompositorState CompositorState;

struct WindowInfo;
typedef struct WindowInfo WindowInfo;

// 纹理缓存项
typedef struct VulkanTexture {
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory memory;
    VkSampler sampler;
    uint32_t width;
    uint32_t height;
    VkFormat format;
    uint32_t ref_count;
    uint64_t last_used_time;
    bool is_used;
} VulkanTexture;

// 纹理缓存
typedef struct VulkanTextureCache {
    VulkanTexture* textures;
    uint32_t texture_count;
    uint32_t max_textures;
    VkDevice device;
    uint64_t memory_usage;
    uint64_t max_memory_usage;
} VulkanTextureCache;

// 渲染优化配置
typedef struct VulkanRenderOptimization {
    bool enable_dirty_rects;      // 启用脏区域优化
    bool enable_texture_cache;    // 启用纹理缓存
    bool enable_msaa;             // 启用MSAA
    uint32_t msaa_samples;        // MSAA采样数
    bool enable_async_rendering;  // 启用异步渲染
    bool enable_depth_test;       // 启用深度测试
    bool enable_alpha_blending;   // 启用Alpha混合
    bool enable_scissor_test;     // 启用剪裁测试
    bool use_render_batching;     // 是否使用渲染批次管理
    bool use_instanced_rendering; // 是否使用实例化渲染
    bool use_adaptive_sync;       // 是否使用自适应同步
    float max_anisotropy;         // 最大各向异性过滤
    bool enable_dynamic_rendering;
    int32_t swap_interval;
    float adaptive_sync_min_refresh_rate;
    float adaptive_sync_max_refresh_rate;
} VulkanRenderOptimization;

// 渲染实例
typedef struct RenderInstance {
    float x, y;            // 位置
    float width, height;   // 尺寸
    float z;               // Z顺序
    float opacity;         // 透明度
    float u0, v0, u1, v1;  // 纹理坐标
    uint32_t texture_id;   // 纹理ID
    uint32_t color;        // 颜色（ARGB）
    uint32_t render_flags;  // 渲染标志位
} RenderInstance;

// 渲染命令类型
typedef enum {
    RENDER_COMMAND_WINDOW,            // 渲染窗口
    RENDER_COMMAND_XWAYLAND_WINDOW,   // 渲染Xwayland窗口
    RENDER_COMMAND_TEXTURE,           // 渲染纹理
    RENDER_COMMAND_RECT,              // 渲染矩形
    RENDER_COMMAND_CLEAR,             // 清除屏幕
    RENDER_COMMAND_FLUSH,             // 刷新渲染
    RENDER_COMMAND_DRAW_LINE,         // 绘制线段
    RENDER_COMMAND_DRAW_TEXT,         // 绘制文本
    RENDER_COMMAND_SET_CLIP,          // 设置裁剪区域
    RENDER_COMMAND_RESET_CLIP,        // 重置裁剪区域
    RENDER_COMMAND_SET_BLEND_MODE,    // 设置混合模式
    RENDER_COMMAND_SET_TRANSFORM      // 设置变换矩阵
} RenderCommandType;

// 渲染命令
typedef struct RenderCommand {
    RenderCommandType type;    // 命令类型
    void* data;               // 命令数据
    uint64_t timestamp;       // 时间戳
    int32_t priority;         // 优先级
} RenderCommand;

// 性能监控数据
typedef struct VulkanPerfStats {
    int64_t frame_start_time;
    int64_t render_time;
    int64_t present_time;
    int64_t total_time;
    uint32_t draw_calls;
    uint32_t texture_switches;
    uint32_t vertices_count;
    uint32_t triangles_count;
    uint64_t frame_count;
    uint64_t last_frame_time_ms;
    uint32_t current_fps;
    uint32_t memory_usage_mb;
    uint32_t frame_buffer_count;  // 帧缓冲数量
} VulkanPerfStats;

// 渲染批次
typedef struct VulkanRenderBatch {
    VkCommandBuffer command_buffer;
    VkPipeline pipeline;
    uint32_t texture_id;
    uint32_t vertex_count;
    uint32_t first_vertex;
    uint32_t instance_count;
    uint32_t first_instance;
    uint32_t batch_id;
    bool is_submitted;
    uint32_t layer_id;  // 渲染层ID
} VulkanRenderBatch;

// 渲染队列
typedef struct VulkanRenderQueue {
    VulkanRenderBatch* batches;
    uint32_t batch_count;
    uint32_t batch_capacity;
    uint32_t next_batch_id;
} VulkanRenderQueue;

// Vulkan状态结构体
typedef struct VulkanState {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    VkCommandPool command_pool;
    VkRenderPass render_pass;
    
    // 交换链资源
    uint32_t image_count;
    VkImage* swapchain_images;
    VkImageView* swapchain_image_views;
    VkFramebuffer* framebuffers;
    
    // 命令缓冲区
    VkCommandBuffer* command_buffers;
    VkCommandPool transfer_command_pool;
    VkCommandBuffer transfer_command_buffer;
    
    // 同步原语
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkSemaphore transfer_semaphore;
    VkFence* in_flight_fences;
    VkFence transfer_fence;
    
    // 渲染管线
    VkPipeline graphics_pipeline;
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet* descriptor_sets;
    
    // 着色器模块
    VkShaderModule vertex_shader_module;
    VkShaderModule fragment_shader_module;
    
    // 纹理和缓冲区
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_buffer_memory;
    
    // 纹理缓存
    VulkanTextureCache texture_cache;
    
    // 渲染优化
    VulkanRenderOptimization optimization;
    
    // 渲染队列
    VulkanRenderQueue render_queue;
    
    // 性能监控
    VulkanPerfStats perf_stats;
    
    // 其他状态
    uint32_t current_frame;
    bool vsync_enabled;
    bool validation_enabled;
    bool is_initialized;
    bool needs_rebuild;
    
    // 多窗口管理
    void** window_render_data;
    uint32_t window_count;
    
    // 内存分配统计
    size_t device_memory_used;
    size_t device_memory_limit;
    
    // 扩展功能
    bool supports_variable_refresh_rate;
    int32_t preferred_fps;
    int32_t max_frames_in_flight;
    
    // 内存池管理
    void* memory_pool;
} VulkanState;

// 核心函数声明
void compositor_vulkan_set_state(CompositorState* state);
CompositorState* compositor_vulkan_get_state(void);
VulkanState* get_vulkan_state(void);

#endif // COMPOSITOR_VULKAN_CORE_H