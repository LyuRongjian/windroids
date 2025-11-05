#ifndef COMPOSITOR_VULKAN_H
#define COMPOSITOR_VULKAN_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <stdbool.h>
#include <android/native_window.h>
#include <stdint.h>

// 前向声明
struct CompositorState;
typedef struct CompositorState CompositorState;

struct WindowInfo;
typedef struct WindowInfo WindowInfo;

// 纹理缓存项
typedef struct {
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
typedef struct {
    VulkanTexture* textures;
    uint32_t texture_count;
    uint32_t max_textures;
    VkDevice device;
} VulkanTextureCache;

// 渲染优化配置
typedef struct {
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
} VulkanRenderOptimization;

// 渲染实例
typedef struct {
    float x, y;            // 位置
    float width, height;   // 尺寸
    float z;               // Z顺序
    float opacity;         // 透明度
    float u0, v0, u1, v1;  // 纹理坐标
    uint32_t texture_id;   // 纹理ID
    uint32_t color;        // 颜色（ARGB）
} RenderInstance;

// 渲染命令类型
typedef enum {
    RENDER_COMMAND_WINDOW,            // 渲染窗口
    RENDER_COMMAND_XWAYLAND_WINDOW,   // 渲染Xwayland窗口
    RENDER_COMMAND_TEXTURE,           // 渲染纹理
    RENDER_COMMAND_RECT,              // 渲染矩形
    RENDER_COMMAND_CLEAR,             // 清除屏幕
    RENDER_COMMAND_FLUSH              // 刷新渲染
} RenderCommandType;

// 渲染命令
typedef struct {
    RenderCommandType type;    // 命令类型
    void* data;               // 命令数据
    uint64_t timestamp;       // 时间戳
} RenderCommand;

// 性能监控数据
typedef struct {
    int64_t frame_start_time;
    int64_t render_time;
    int64_t present_time;
    int64_t total_time;
    uint32_t draw_calls;
    uint32_t texture_switches;
    uint32_t vertices_count;
    uint32_t triangles_count;
} VulkanPerfStats;

// 渲染批次
typedef struct {
    VkCommandBuffer command_buffer;
    VkPipeline pipeline;
    uint32_t texture_id;
    uint32_t vertex_count;
    uint32_t first_vertex;
    uint32_t instance_count;
    uint32_t first_instance;
} VulkanRenderBatch;

// 渲染队列
typedef struct {
    VulkanRenderBatch* batches;
    uint32_t batch_count;
    uint32_t batch_capacity;
} VulkanRenderQueue;

// Vulkan状态结构体
typedef struct {
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
} VulkanState;

// Vulkan初始化相关函数
int init_vulkan(CompositorState* state);
int load_vulkan_functions(VulkanState* vulkan);
int create_vulkan_instance(VulkanState* vulkan, bool enable_validation);
int select_physical_device(VulkanState* vulkan);
int create_logical_device(VulkanState* vulkan, bool enable_vsync);
int create_command_pool(VulkanState* vulkan);
int create_transfer_command_pool(VulkanState* vulkan);
int create_swapchain(VulkanState* vulkan, ANativeWindow* window, int width, int height);
int create_render_pass(VulkanState* vulkan);
int create_descriptor_set_layout(VulkanState* vulkan);
int create_pipeline_layout(VulkanState* vulkan);
int create_graphics_pipeline(VulkanState* vulkan);
int create_descriptor_pool(VulkanState* vulkan);
int create_descriptor_sets(VulkanState* vulkan);
int create_framebuffers(VulkanState* vulkan);
int create_command_buffers(VulkanState* vulkan);
int create_sync_objects(VulkanState* vulkan);
int create_vertex_buffer(VulkanState* vulkan);
int create_texture_cache(VulkanState* vulkan);

// 渲染相关函数
int render_frame(void);
int recreate_swapchain(int width, int height);
int render_dirty_rects(CompositorState* state, VkCommandBuffer command_buffer);
int render_with_scissors(CompositorState* state, VkCommandBuffer command_buffer, const DirtyRect* rects, int count);

// 纹理管理函数
int create_texture_from_surface(VulkanState* vulkan, void* surface, uint32_t* out_texture_id);
int update_texture_from_surface(VulkanState* vulkan, uint32_t texture_id, void* surface);
int destroy_texture(VulkanState* vulkan, uint32_t texture_id);
int get_texture(VulkanState* vulkan, uint32_t texture_id, VulkanTexture** out_texture);

// 渲染优化函数
void optimize_render_commands(CompositorState* state);
void cull_invisible_windows(CompositorState* state);
void merge_dirty_rects(CompositorState* state);

// 性能监控函数
void begin_perf_tracking(VulkanState* vulkan);
void end_perf_tracking(VulkanState* vulkan);
void log_perf_stats(VulkanState* vulkan);

// 资源清理函数
void cleanup_swapchain_resources(VulkanState* vulkan);
void cleanup_texture_cache(VulkanState* vulkan);
void cleanup_vulkan_resources(VulkanState* vulkan);
void cleanup_vulkan(void);

// 初始化渲染批次管理
int init_render_batches(VulkanState* vulkan);

// 初始化渲染队列
int init_render_queue(VulkanState* vulkan);

// 添加渲染命令到队列
int add_render_command(VulkanState* vulkan, RenderCommandType type, void* data);

// 优化渲染批次
int optimize_render_batches(VulkanState* vulkan);

// 执行渲染队列
int execute_render_queue(VulkanState* vulkan, VkCommandBuffer command_buffer);

// 更新性能统计信息
void update_vulkan_performance_stats(VulkanState* vulkan);

// 使用硬件加速渲染窗口
int render_windows_with_hardware_acceleration(CompositorState* state);

// 内部函数
void wait_idle(void);
int acquire_next_image(VulkanState* vulkan, uint32_t* image_index);
int begin_rendering(VulkanState* vulkan, uint32_t image_index);
int end_rendering(VulkanState* vulkan);
int submit_rendering(VulkanState* vulkan, uint32_t image_index);
void render_background(CompositorState* state, VkCommandBuffer command_buffer);
void render_windows(CompositorState* state, VkCommandBuffer command_buffer);
void render_window(CompositorState* state, WindowInfo* window, bool is_wayland, VkCommandBuffer command_buffer);
void render_window_with_dirty_rects(CompositorState* state, WindowInfo* window, bool is_wayland, VkCommandBuffer command_buffer);

// 多窗口渲染函数
int prepare_window_rendering(CompositorState* state, void* window_ptr, bool is_wayland);
int finish_window_rendering(CompositorState* state, void* window_ptr, bool is_wayland);

// 设置全局状态指针（内部使用）
void compositor_vulkan_set_state(CompositorState* state);

#endif // COMPOSITOR_VULKAN_H