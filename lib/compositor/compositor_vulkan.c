#include "compositor_vulkan.h"
#include "compositor_utils.h"
#include "compositor.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 设置全局状态指针（由compositor.c调用）
void compositor_vulkan_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 初始化Vulkan
int init_vulkan(CompositorState* state) {
    if (!state) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid compositor state");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Initializing Vulkan...");
    
    // 重置Vulkan相关状态
    memset(&state->vulkan, 0, sizeof(VulkanState));
    
    // 初始化性能监控
    memset(&state->vulkan.perf_monitor, 0, sizeof(RenderPerfMonitor));
    state->vulkan.perf_monitor.start_time = get_current_time_ms();
    
    // 初始化渲染优化配置
    state->vulkan.render_optimization.enabled = state->config.use_dirty_rect_optimization;
    state->vulkan.render_optimization.use_clipping = state->config.enable_clip_test;
    
    // 初始化多窗口管理
    state->vulkan.multi_window.active_batch_count = 0;
    state->vulkan.multi_window.window_count = 0;
    state->vulkan.multi_window.active_windows = NULL;
    
    // 初始化内存分配统计
    state->vulkan.mem_stats.total_allocated = 0;
    state->vulkan.mem_stats.peak_allocated = 0;
    state->vulkan.mem_stats.texture_memory = 0;
    state->vulkan.mem_stats.buffer_memory = 0;
    
    // 初始化surface纹理缓存相关字段
    state->vulkan.surface_texture_cache = NULL;
    state->vulkan.surface_texture_count = 0;
    
    // 加载Vulkan函数
    if (load_vulkan_functions(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to load Vulkan functions");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 创建实例
    if (create_vulkan_instance(&state->vulkan, state->config.enable_debug_logging) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to create Vulkan instance");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 选择物理设备
    if (select_physical_device(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to select physical device");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 创建逻辑设备
    if (create_logical_device(&state->vulkan, state->config.enable_vsync) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to create logical device");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 创建命令池
    if (create_command_pool(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to create command pool");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 创建传输命令池
    if (create_transfer_command_pool(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to create transfer command pool");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 创建交换链
    if (create_swapchain(&state->vulkan, state->window, state->width, state->height) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to create swapchain");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 创建渲染通道
    if (create_render_pass(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to create render pass");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 创建帧缓冲
    if (create_framebuffers(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to create framebuffers");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 创建命令缓冲区
    if (create_command_buffers(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to create command buffers");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 创建同步原语
    if (create_sync_objects(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to create sync objects");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 初始化着色器
    if (init_shaders(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize shaders");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 初始化描述符集
    if (init_descriptor_sets(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize descriptor sets");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 初始化渲染管线
    if (init_render_pipelines(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize render pipelines");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 初始化纹理缓存
    if (init_texture_cache(&state->vulkan, state->config.texture_cache_size) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize texture cache");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 初始化基于surface的纹理缓存
    if (init_surface_texture_cache(&state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize surface texture cache");
        cleanup_texture_cache(&state->vulkan);
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Vulkan initialization completed successfully");
    return COMPOSITOR_OK;
}

// 加载Vulkan函数
int load_vulkan_functions(VulkanState* vulkan) {
    // 这里应该实现Vulkan函数的加载
    // 实际代码会使用vkGetInstanceProcAddr等函数
    log_message(COMPOSITOR_LOG_DEBUG, "Loading Vulkan functions");
    return COMPOSITOR_OK;
}

// 创建Vulkan实例
int create_vulkan_instance(VulkanState* vulkan, bool enable_validation) {
    // 实现Vulkan实例创建逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Creating Vulkan instance, validation layers: %s", 
               enable_validation ? "enabled" : "disabled");
    return COMPOSITOR_OK;
}

// 选择物理设备
int select_physical_device(VulkanState* vulkan) {
    // 实现物理设备选择逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Selecting physical device");
    return COMPOSITOR_OK;
}

// 创建逻辑设备
int create_logical_device(VulkanState* vulkan, bool enable_vsync) {
    // 实现逻辑设备创建逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Creating logical device, vsync: %s", 
               enable_vsync ? "enabled" : "disabled");
    return COMPOSITOR_OK;
}

// 创建命令池
int create_command_pool(VulkanState* vulkan) {
    // 实现命令池创建逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Creating command pool");
    return COMPOSITOR_OK;
}

// 创建交换链
int create_swapchain(VulkanState* vulkan, ANativeWindow* window, int width, int height) {
    // 实现交换链创建逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Creating swapchain for window %dx%d", width, height);
    return COMPOSITOR_OK;
}

// 创建渲染通道
int create_render_pass(VulkanState* vulkan) {
    // 实现渲染通道创建逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Creating render pass");
    return COMPOSITOR_OK;
}

// 创建帧缓冲
int create_framebuffers(VulkanState* vulkan) {
    // 实现帧缓冲创建逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Creating framebuffers");
    return COMPOSITOR_OK;
}

// 创建命令缓冲区
int create_command_buffers(VulkanState* vulkan) {
    // 实现命令缓冲区创建逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Creating command buffers");
    return COMPOSITOR_OK;
}

// 创建同步原语
int create_sync_objects(VulkanState* vulkan) {
    // 实现同步原语创建逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Creating sync objects");
    return COMPOSITOR_OK;
}

// 初始化着色器
int init_shaders(VulkanState* vulkan) {
    // 实现着色器初始化逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Initializing shaders");
    return COMPOSITOR_OK;
}

// 初始化描述符集
int init_descriptor_sets(VulkanState* vulkan) {
    // 实现描述符集初始化逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Initializing descriptor sets");
    return COMPOSITOR_OK;
}

// 重建交换链
int recreate_swapchain(int width, int height) {
    if (!g_compositor_state) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Recreating swapchain for new size: %dx%d", width, height);
    
    // 等待所有命令完成
    wait_idle();
    
    // 清理旧的交换链资源
    cleanup_swapchain_resources(&g_compositor_state->vulkan);
    
    // 创建新的交换链
    if (create_swapchain(&g_compositor_state->vulkan, 
                         g_compositor_state->window, 
                         width, height) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to recreate swapchain");
        return COMPOSITOR_ERROR_SWAPCHAIN_ERROR;
    }
    
    // 重新创建其他相关资源
    if (create_render_pass(&g_compositor_state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to recreate render pass");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    if (create_framebuffers(&g_compositor_state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to recreate framebuffers");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    if (create_command_buffers(&g_compositor_state->vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to recreate command buffers");
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 更新渲染优化配置中的尺寸信息
    g_compositor_state->vulkan.render_optimization.screen_width = width;
    g_compositor_state->vulkan.render_optimization.screen_height = height;
    
    log_message(COMPOSITOR_LOG_INFO, "Swapchain recreated successfully");
    return COMPOSITOR_OK;
}

// 清理交换链资源
void cleanup_swapchain_resources(VulkanState* vulkan) {
    // 实现交换链资源清理逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Cleaning up swapchain resources");
}

// 等待设备空闲
void wait_idle(void) {
    if (!g_compositor_state) return;
    log_message(COMPOSITOR_LOG_DEBUG, "Waiting for device idle");
    // 实际代码会调用vkDeviceWaitIdle
}

// 渲染一帧
int render_frame(void) {
    if (!g_compositor_state) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    VulkanState* vulkan = &g_compositor_state->vulkan;
    RenderPerfMonitor* perf = &vulkan->perf_monitor;
    
    // 记录帧开始时间
    int64_t frame_start_time = get_current_time_ms();
    perf->current_frame_time = frame_start_time;
    
    // 获取下一帧图像
    uint32_t image_index;
    int64_t acquire_start_time = get_current_time_ms();
    if (acquire_next_image(vulkan, &image_index) != COMPOSITOR_OK) {
        return COMPOSITOR_ERROR_SWAPCHAIN_ERROR;
    }
    perf->image_acquire_time = get_current_time_ms() - acquire_start_time;
    
    // 开始渲染
    if (begin_rendering(vulkan, image_index) != COMPOSITOR_OK) {
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 检查是否使用脏区域优化
    bool use_dirty_rect = vulkan->render_optimization.enabled && 
                          g_compositor_state->dirty_rect_count > 0;
    
    // 渲染背景（如果需要）
    if (!use_dirty_rect || g_compositor_state->dirty_rect_count > 10) {
        // 如果脏区域太多，直接渲染整个背景
        render_background(g_compositor_state);
    } else {
        // 只渲染脏区域的背景部分
        render_background_dirty(g_compositor_state);
    }
    
    // 准备渲染批次
    prepare_render_batches(g_compositor_state);
    
    // 渲染所有窗口
    int64_t render_start_time = get_current_time_ms();
    render_windows(g_compositor_state);
    perf->render_time = get_current_time_ms() - render_start_time;
    
    // 结束渲染
    if (end_rendering(vulkan) != COMPOSITOR_OK) {
        return COMPOSITOR_ERROR_VULKAN;
    }
    
    // 提交渲染
    int64_t submit_start_time = get_current_time_ms();
    if (submit_rendering(vulkan, image_index) != COMPOSITOR_OK) {
        return COMPOSITOR_ERROR_VULKAN;
    }
    perf->submit_time = get_current_time_ms() - submit_start_time;
    
    // 更新性能统计
    perf->frame_count++;
    perf->total_frame_time += get_current_time_ms() - frame_start_time;
    if (perf->frame_count % 60 == 0) {  // 每60帧更新一次FPS
        int64_t current_time = get_current_time_ms();
        perf->fps = (float)60 * 1000.0f / (current_time - perf->last_fps_time);
        perf->last_fps_time = current_time;
        perf->avg_frame_time = (float)perf->total_frame_time / perf->frame_count;
        
        if (g_compositor_state->config.show_fps_counter) {
            log_message(COMPOSITOR_LOG_INFO, "FPS: %.1f, Avg. frame time: %.2fms", 
                       perf->fps, perf->avg_frame_time);
        }
    }
    
    // 清理脏区域
    compositor_clear_dirty_rects(g_compositor_state);
    
    return COMPOSITOR_OK;
}

// 获取下一帧图像
int acquire_next_image(VulkanState* vulkan, uint32_t* image_index) {
    // 实现获取下一帧图像的逻辑
    return COMPOSITOR_OK;
}

// 开始渲染
int begin_rendering(VulkanState* vulkan, uint32_t image_index) {
    // 实现开始渲染的逻辑
    return COMPOSITOR_OK;
}

// 结束渲染
int end_rendering(VulkanState* vulkan) {
    // 实现结束渲染的逻辑
    return COMPOSITOR_OK;
}

// 提交渲染
int submit_rendering(VulkanState* vulkan, uint32_t image_index) {
    // 实现提交渲染的逻辑
    return COMPOSITOR_OK;
}

// 渲染背景
void render_background(CompositorState* state) {
    // 实现背景渲染逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Rendering background");
}

// 渲染所有窗口
void render_windows(CompositorState* state) {
    VulkanState* vulkan = &state->vulkan;
    
    // 检查是否使用脏区域优化
    bool use_dirty_rect = vulkan->render_optimization.enabled && 
                          state->dirty_rect_count > 0;
    
    // 按z_order排序窗口
    compositor_sort_windows_by_z_order(state);
    
    // 渲染Xwayland窗口（按z_order顺序）
    XwaylandWindowState* xwayland_state = &state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        XwaylandWindowState* window = xwayland_state->windows[i];
        
        // 检查窗口是否需要渲染
        if (window->state == WINDOW_STATE_MINIMIZED || window->surface == NULL) {
            continue;
        }
        
        // 脏区域优化检查
        if (use_dirty_rect && !window->is_dirty && !check_window_intersects_dirty_rect(state, window, false)) {
            continue;  // 窗口与脏区域不相交，跳过渲染
        }
        
        // 准备窗口信息
        WindowInfo info = {
            .title = window->title,
            .x = window->x,
            .y = window->y,
            .width = window->width,
            .height = window->height,
            .state = window->state,
            .opacity = window->opacity,
            .z_order = window->z_order,
            .is_wayland = false
        };
        
        // 记录渲染统计
        vulkan->perf_monitor.windows_rendered++;
        
        // 渲染窗口
        render_window(state, &info, false);
        
        // 清除窗口脏标记
        window->is_dirty = false;
        window->dirty_region_count = 0;
    }
    
    // 渲染Wayland窗口（按z_order顺序）
    WaylandWindowState* wayland_state = &state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        WaylandWindow* window = wayland_state->windows[i];
        
        // 检查窗口是否需要渲染
        if (window->state == WINDOW_STATE_MINIMIZED || window->surface == NULL) {
            continue;
        }
        
        // 脏区域优化检查
        if (use_dirty_rect && !window->is_dirty && !check_window_intersects_dirty_rect(state, window, true)) {
            continue;  // 窗口与脏区域不相交，跳过渲染
        }
        
        // 准备窗口信息
        WindowInfo info = {
            .title = window->title,
            .x = window->x,
            .y = window->y,
            .width = window->width,
            .height = window->height,
            .state = window->state,
            .opacity = window->opacity,
            .z_order = window->z_order,
            .is_wayland = true
        };
        
        // 记录渲染统计
        vulkan->perf_monitor.windows_rendered++;
        
        // 渲染窗口
        render_window(state, &info, true);
        
        // 清除窗口脏标记
        window->is_dirty = false;
        window->dirty_region_count = 0;
    }
    
    // 输出性能调试信息
    if (state->config.debug_mode && vulkan->perf_monitor.frame_count % 60 == 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "Rendered %d windows, FPS: %.1f, Batch count: %d",
                   vulkan->perf_monitor.windows_rendered,
                   vulkan->perf_monitor.fps,
                   vulkan->multi_window.active_batch_count);
        vulkan->perf_monitor.windows_rendered = 0;
    }
}

// 渲染单个窗口
void render_window(CompositorState* state, WindowInfo* window, bool is_wayland) {
    if (!state || !window) return;
    
    VulkanState* vulkan = &state->vulkan;
    
    // 检查窗口是否在可见区域内
    if (window->x > state->width || window->y > state->height ||
        window->x + window->width < 0 || window->y + window->height < 0) {
        return; // 窗口完全不可见，跳过渲染
    }
    
    // 计算实际可见区域（处理窗口超出屏幕边界的情况）
    int visible_x = (window->x < 0) ? -window->x : 0;
    int visible_y = (window->y < 0) ? -window->y : 0;
    int visible_width = window->width;
    int visible_height = window->height;
    
    if (window->x + window->width > state->width) {
        visible_width = state->width - window->x;
    }
    
    if (window->y + window->height > state->height) {
        visible_height = state->height - window->y;
    }
    
    // 确保可见区域有效
    if (visible_width <= 0 || visible_height <= 0) {
        return;
    }
    
    // 获取窗口表面和纹理ID
    void* window_surface = NULL;
    uint32_t texture_id = UINT32_MAX;
    
    if (is_wayland) {
        // 从Wayland窗口获取表面
        WaylandWindow* wayland_window = NULL;
        for (int i = 0; i < state->wayland_state.window_count; i++) {
            if (strcmp(state->wayland_state.windows[i]->title, window->title) == 0 &&
                state->wayland_state.windows[i]->x == window->x &&
                state->wayland_state.windows[i]->y == window->y) {
                wayland_window = state->wayland_state.windows[i];
                window_surface = wayland_window->surface;
                break;
            }
        }
    } else {
        // 从Xwayland窗口获取表面
        XwaylandWindowState* xwayland_window = NULL;
        for (int i = 0; i < state->xwayland_state.window_count; i++) {
            if (strcmp(state->xwayland_state.windows[i]->title, window->title) == 0 &&
                state->xwayland_state.windows[i]->x == window->x &&
                state->xwayland_state.windows[i]->y == window->y) {
                xwayland_window = state->xwayland_state.windows[i];
                window_surface = xwayland_window->surface;
                break;
            }
        }
    }
    
    // 如果找到了表面，尝试获取或创建纹理
    if (window_surface) {
        // 使用新的基于surface的纹理缓存系统
        texture_id = get_cached_texture_by_surface(vulkan, window_surface);
        
        if (texture_id != UINT32_MAX) {
            // 纹理ID有效，获取实际纹理
            VulkanTexture* texture = NULL;
            if (get_texture(vulkan, texture_id, &texture) == COMPOSITOR_OK && texture) {
                // 在实际实现中，这里会：
                // 1. 绑定纹理到描述符集
                // 2. 更新统一缓冲区（包含窗口位置、大小、透明度等信息）
                // 3. 设置视口和裁剪区域
                // 4. 绘制四边形，使用适当的着色器处理窗口内容
                
                if (state->config.debug_mode) {
                    log_message(COMPOSITOR_LOG_DEBUG, "Using cached texture for window: %s (ID: %u)",
                               window->title ? window->title : "(untitled)", texture_id);
                }
            }
        }
    }
    
    // 如果没有纹理，使用默认颜色渲染（调试用）
    if (!surface_texture) {
        if (state->config.debug_mode) {
            log_message(COMPOSITOR_LOG_DEBUG, "Window texture not found, using placeholder");
        }
        
        // 在实际实现中，这里会使用Vulkan命令绘制一个简单的矩形作为占位符
        // 例如使用纯色填充或者纹理坐标映射到特殊纹理
    } else {
        // 在实际实现中，这里会设置Vulkan命令：
        // 1. 绑定纹理到描述符集
        // 2. 更新统一缓冲区（包含窗口位置、大小、透明度等信息）
        // 3. 设置视口和裁剪区域
        // 4. 绘制四边形，使用适当的着色器处理窗口内容
    }
    
    // 处理窗口透明度
    if (window->opacity < 1.0f) {
        // 在实际实现中，这里会设置混合模式来处理透明度
        // vulkan->current_command_buffer中的混合设置
    }
    
    // 脏区域优化：如果启用了脏区域优化，只渲染窗口的脏区域
    if (vulkan->render_optimization.enabled && window->dirty_region_count > 0) {
        // 在实际实现中，这里会遍历窗口的脏区域，并只为每个脏区域生成绘制命令
        // 这可以显著提高渲染性能，特别是在窗口只有部分更新时
    }
    
    if (state->config.debug_mode) {
        log_message(COMPOSITOR_LOG_DEBUG, "Rendering window: %s at %d,%d with size %dx%d (visible: %dx%d)", 
                   window->title ? window->title : "(untitled)", 
                   window->x, window->y, window->width, window->height,
                   visible_width, visible_height);
    }
}

// 清理Vulkan资源
void cleanup_vulkan(void) {
    if (!g_compositor_state) return;
    
    log_message(COMPOSITOR_LOG_INFO, "Cleaning up Vulkan resources");
    
    // 等待所有命令完成
    wait_idle();
    
    // 清理纹理缓存
    cleanup_texture_cache(&g_compositor_state->vulkan);
    
    // 清理交换链资源
    cleanup_swapchain_resources(&g_compositor_state->vulkan);
    
    // 清理多窗口管理资源
    cleanup_multi_window_resources(&g_compositor_state->vulkan);
    
    // 清理其他Vulkan资源
    cleanup_vulkan_resources(&g_compositor_state->vulkan);
    
    // 输出内存使用统计
    VulkanMemStats* mem_stats = &g_compositor_state->vulkan.mem_stats;
    log_message(COMPOSITOR_LOG_INFO, "Vulkan memory usage - Peak: %.2f MB, Texture: %.2f MB, Buffer: %.2f MB",
               mem_stats->peak_allocated / (1024.0f * 1024.0f),
               mem_stats->texture_memory / (1024.0f * 1024.0f),
               mem_stats->buffer_memory / (1024.0f * 1024.0f));
    
    log_message(COMPOSITOR_LOG_INFO, "Vulkan cleanup completed");
}

// 清理Vulkan资源（内部函数）
void cleanup_vulkan_resources(VulkanState* vulkan) {
    // 清理渲染管线
    if (vulkan->render_pipeline.pipeline) {
        // 实际代码会调用vkDestroyPipeline
        log_message(COMPOSITOR_LOG_DEBUG, "Destroying render pipeline");
    }
    
    if (vulkan->render_pipeline.pipeline_layout) {
        // 实际代码会调用vkDestroyPipelineLayout
        log_message(COMPOSITOR_LOG_DEBUG, "Destroying pipeline layout");
    }
    
    // 清理传输命令池
    if (vulkan->transfer_command_pool) {
        // 实际代码会调用vkDestroyCommandPool
        log_message(COMPOSITOR_LOG_DEBUG, "Destroying transfer command pool");
    }
    
    // 清理其他通用资源
    log_message(COMPOSITOR_LOG_DEBUG, "Cleaning up general Vulkan resources");
}

// 创建传输命令池
int create_transfer_command_pool(VulkanState* vulkan) {
    log_message(COMPOSITOR_LOG_DEBUG, "Creating transfer command pool");
    // 实际代码会创建专门用于传输操作的命令池
    return COMPOSITOR_OK;
}

// 初始化渲染管线
int init_render_pipelines(VulkanState* vulkan) {
    log_message(COMPOSITOR_LOG_DEBUG, "Initializing render pipelines");
    
    // 初始化主渲染管线
    memset(&vulkan->render_pipeline, 0, sizeof(RenderPipeline));
    vulkan->render_pipeline.name = "MainWindowPipeline";
    vulkan->render_pipeline.blend_enabled = true;
    vulkan->render_pipeline.depth_test_enabled = false;
    
    // 初始化特效渲染管线（如阴影、模糊等）
    memset(&vulkan->effect_pipeline, 0, sizeof(RenderPipeline));
    vulkan->effect_pipeline.name = "EffectPipeline";
    vulkan->effect_pipeline.blend_enabled = true;
    vulkan->effect_pipeline.depth_test_enabled = false;
    
    return COMPOSITOR_OK;
}

// 初始化纹理缓存
int init_texture_cache(VulkanState* vulkan, size_t max_size_bytes) {
    log_message(COMPOSITOR_LOG_DEBUG, "Initializing texture cache with max size: %.2f MB",
               max_size_bytes / (1024.0f * 1024.0f));
    
    // 初始化纹理缓存
    memset(&vulkan->texture_cache, 0, sizeof(TextureCache));
    vulkan->texture_cache.max_size_bytes = max_size_bytes;
    vulkan->texture_cache.current_size_bytes = 0;
    vulkan->texture_cache.textures = NULL;
    vulkan->texture_cache.texture_count = 0;
    vulkan->texture_cache.capacity = 0;
    
    // 分配初始容量
    vulkan->texture_cache.capacity = 64; // 初始可存储64个纹理
    vulkan->texture_cache.textures = (TextureCacheEntry*)malloc(
        sizeof(TextureCacheEntry) * vulkan->texture_cache.capacity);
    
    if (!vulkan->texture_cache.textures) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate texture cache");
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    // 初始化缓存条目
    for (int i = 0; i < vulkan->texture_cache.capacity; i++) {
        memset(&vulkan->texture_cache.textures[i], 0, sizeof(TextureCacheEntry));
    }
    
    // 初始化基于surface的纹理缓存
    if (init_surface_texture_cache(vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize surface texture cache");
        // 清理已分配的资源
        free(vulkan->texture_cache.textures);
        memset(&vulkan->texture_cache, 0, sizeof(TextureCache));
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    return COMPOSITOR_OK;
}

// 清理纹理缓存
void cleanup_texture_cache(VulkanState* vulkan) {
    if (!vulkan->texture_cache.textures) {
        return;
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Cleaning up texture cache, %d textures to release",
               vulkan->texture_cache.texture_count);
    
    // 释放所有纹理资源
    for (int i = 0; i < vulkan->texture_cache.texture_count; i++) {
        TextureCacheEntry* entry = &vulkan->texture_cache.textures[i];
        if (entry->texture) {
            // 实际代码会释放Vulkan纹理资源
            log_message(COMPOSITOR_LOG_DEBUG, "Releasing cached texture: %s", entry->name ? entry->name : "unnamed");
        }
        if (entry->name) {
            free((void*)entry->name);
        }
    }
    
    // 释放缓存数组
    free(vulkan->texture_cache.textures);
    memset(&vulkan->texture_cache, 0, sizeof(TextureCache));
    
    // 清理基于surface的纹理缓存
    cleanup_surface_texture_cache(vulkan);
}

// 缓存纹理
int cache_texture(VulkanState* vulkan, const char* name, void* texture, size_t size_bytes) {
    // 查找是否已存在
    for (int i = 0; i < vulkan->texture_cache.texture_count; i++) {
        if (vulkan->texture_cache.textures[i].name && name && 
            strcmp(vulkan->texture_cache.textures[i].name, name) == 0) {
            // 更新现有条目
            vulkan->texture_cache.textures[i].last_used = get_current_time_ms();
            vulkan->texture_cache.textures[i].texture = texture;
            vulkan->texture_cache.textures[i].size_bytes = size_bytes;
            return COMPOSITOR_OK;
        }
    }
    
    // 检查是否需要扩容
    if (vulkan->texture_cache.texture_count >= vulkan->texture_cache.capacity) {
        int new_capacity = vulkan->texture_cache.capacity * 2;
        TextureCacheEntry* new_textures = (TextureCacheEntry*)realloc(
            vulkan->texture_cache.textures, sizeof(TextureCacheEntry) * new_capacity);
        
        if (!new_textures) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to resize texture cache");
            return COMPOSITOR_ERROR_MEMORY;
        }
        
        vulkan->texture_cache.textures = new_textures;
        vulkan->texture_cache.capacity = new_capacity;
        
        // 初始化新分配的条目
        for (int i = vulkan->texture_cache.texture_count; i < new_capacity; i++) {
            memset(&vulkan->texture_cache.textures[i], 0, sizeof(TextureCacheEntry));
        }
    }
    
    // 检查是否需要释放旧纹理以腾出空间
    while (vulkan->texture_cache.current_size_bytes + size_bytes > vulkan->texture_cache.max_size_bytes &&
           vulkan->texture_cache.texture_count > 0) {
        // 找到最旧的未使用纹理
        int oldest_index = 0;
        int64_t oldest_time = vulkan->texture_cache.textures[0].last_used;
        
        for (int i = 1; i < vulkan->texture_cache.texture_count; i++) {
            if (vulkan->texture_cache.textures[i].last_used < oldest_time) {
                oldest_time = vulkan->texture_cache.textures[i].last_used;
                oldest_index = i;
            }
        }
        
        // 释放最旧的纹理
        TextureCacheEntry* entry = &vulkan->texture_cache.textures[oldest_index];
        if (entry->texture) {
            // 实际代码会释放Vulkan纹理资源
            log_message(COMPOSITOR_LOG_DEBUG, "Evicting cached texture: %s", entry->name ? entry->name : "unnamed");
        }
        if (entry->name) {
            free((void*)entry->name);
        }
        
        // 更新缓存统计
        vulkan->texture_cache.current_size_bytes -= entry->size_bytes;
        vulkan->mem_stats.texture_memory -= entry->size_bytes;
        
        // 移除条目（将最后一个条目移到当前位置）
        if (oldest_index < vulkan->texture_cache.texture_count - 1) {
            vulkan->texture_cache.textures[oldest_index] = 
                vulkan->texture_cache.textures[vulkan->texture_cache.texture_count - 1];
        }
        vulkan->texture_cache.texture_count--;
    }
    
    // 添加新纹理
    TextureCacheEntry* new_entry = &vulkan->texture_cache.textures[vulkan->texture_cache.texture_count++];
    new_entry->texture = texture;
    new_entry->size_bytes = size_bytes;
    new_entry->last_used = get_current_time_ms();
    new_entry->name = name ? strdup(name) : NULL;
    
    // 更新缓存统计
    vulkan->texture_cache.current_size_bytes += size_bytes;
    vulkan->mem_stats.texture_memory += size_bytes;
    
    // 更新峰值内存使用
    if (vulkan->mem_stats.total_allocated + size_bytes > vulkan->mem_stats.peak_allocated) {
        vulkan->mem_stats.peak_allocated = vulkan->mem_stats.total_allocated + size_bytes;
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Cached texture: %s, size: %.2f KB",
               name ? name : "unnamed", size_bytes / 1024.0f);
    
    return COMPOSITOR_OK;
}

// 获取缓存的纹理（基于名称）
void* get_cached_texture(VulkanState* vulkan, const char* name) {
    for (int i = 0; i < vulkan->texture_cache.texture_count; i++) {
        if (vulkan->texture_cache.textures[i].name && name && 
            strcmp(vulkan->texture_cache.textures[i].name, name) == 0) {
            // 更新使用时间
            vulkan->texture_cache.textures[i].last_used = get_current_time_ms();
            return vulkan->texture_cache.textures[i].texture;
        }
    }
    return NULL;
}

// 纹理缓存项结构体定义（基于surface指针）
typedef struct SurfaceTextureCacheItem {
    void* surface;            // 原始表面指针（作为键）
    uint32_t texture_id;      // 纹理ID，指向VulkanTextureCache中的纹理
    uint64_t last_used;       // 最后使用时间戳
    struct SurfaceTextureCacheItem* next; // 下一个缓存项（用于链表）
} SurfaceTextureCacheItem;

// 初始化surface纹理缓存
int init_surface_texture_cache(VulkanState* vulkan) {
    // 这里只需要初始化链表头指针和计数
    vulkan->surface_texture_cache = NULL;
    vulkan->surface_texture_count = 0;
    
    if (vulkan->texture_cache.device == VK_NULL_HANDLE) {
        vulkan->texture_cache.device = vulkan->device;
    }
    
    return COMPOSITOR_OK;
}

// 从缓存中获取基于surface的纹理
uint32_t get_cached_texture_by_surface(VulkanState* vulkan, void* surface) {
    if (!vulkan || !surface) return UINT32_MAX;
    
    // 检查surface纹理缓存是否已初始化
    if (vulkan->surface_texture_count == 0 && vulkan->surface_texture_cache == NULL) {
        init_surface_texture_cache(vulkan);
    }
    
    // 在缓存中查找纹理
    SurfaceTextureCacheItem* current = vulkan->surface_texture_cache;
    while (current) {
        if (current->surface == surface) {
            // 找到纹理，更新最后使用时间
            current->last_used = get_current_time_ms();
            return current->texture_id;
        }
        current = current->next;
    }
    
    // 纹理不在缓存中，尝试从surface创建新纹理
    uint32_t new_texture_id = UINT32_MAX;
    int result = create_texture_from_surface(vulkan, surface, &new_texture_id);
    
    if (result == COMPOSITOR_OK && new_texture_id != UINT32_MAX) {
        // 创建成功，添加到surface纹理缓存
        SurfaceTextureCacheItem* new_item = malloc(sizeof(SurfaceTextureCacheItem));
        if (!new_item) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for surface texture cache item");
            return UINT32_MAX;
        }
        
        // 初始化缓存项
        new_item->surface = surface;
        new_item->texture_id = new_texture_id;
        new_item->last_used = get_current_time_ms();
        new_item->next = NULL;
        
        // 添加到链表头部
        new_item->next = vulkan->surface_texture_cache;
        vulkan->surface_texture_cache = new_item;
        vulkan->surface_texture_count++;
        
        // 如果缓存超过大小限制，执行清理
        if (vulkan->surface_texture_count > 128) { // 限制为128个surface
            evict_oldest_surface_texture(vulkan);
        }
        
        return new_texture_id;
    }
    
    return UINT32_MAX;
}

// 清理surface纹理缓存
void cleanup_surface_texture_cache(VulkanState* vulkan) {
    if (!vulkan) return;
    
    SurfaceTextureCacheItem* current = vulkan->surface_texture_cache;
    while (current) {
        SurfaceTextureCacheItem* next = current->next;
        
        // 释放纹理资源
        if (current->texture_id != UINT32_MAX) {
            destroy_texture(vulkan, current->texture_id);
        }
        
        // 释放缓存项内存
        free(current);
        current = next;
    }
    
    // 重置缓存状态
    vulkan->surface_texture_cache = NULL;
    vulkan->surface_texture_count = 0;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Surface texture cache cleaned up");
}

// 辅助函数：驱逐最旧的surface纹理
static void evict_oldest_surface_texture(VulkanState* vulkan) {
    if (!vulkan || !vulkan->surface_texture_cache) return;
    
    SurfaceTextureCacheItem* oldest = vulkan->surface_texture_cache;
    SurfaceTextureCacheItem* oldest_prev = NULL;
    SurfaceTextureCacheItem* current = vulkan->surface_texture_cache;
    SurfaceTextureCacheItem* prev = NULL;
    
    // 查找最旧的纹理
    while (current) {
        if (current->last_used < oldest->last_used) {
            oldest = current;
            oldest_prev = prev;
        }
        prev = current;
        current = current->next;
    }
    
    // 从链表中移除最旧的纹理
    if (oldest_prev) {
        oldest_prev->next = oldest->next;
    } else {
        vulkan->surface_texture_cache = oldest->next;
    }
    
    // 释放纹理资源
    if (oldest->texture_id != UINT32_MAX) {
        destroy_texture(vulkan, oldest->texture_id);
    }
    
    // 释放缓存项内存
    free(oldest);
    vulkan->surface_texture_count--;
}

// 准备渲染批次
void prepare_render_batches(CompositorState* state) {
    VulkanState* vulkan = &state->vulkan;
    
    // 重置批次计数
    vulkan->multi_window.active_batch_count = 0;
    vulkan->multi_window.window_count = 0;
    
    // 统计需要渲染的窗口数量
    int total_windows = 0;
    XwaylandWindowState* xwayland_state = &state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            xwayland_state->windows[i]->surface != NULL) {
            total_windows++;
        }
    }
    
    WaylandWindowState* wayland_state = &state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            wayland_state->windows[i]->surface != NULL) {
            total_windows++;
        }
    }
    
    // 分配或调整活动窗口数组
    if (vulkan->multi_window.window_count < total_windows) {
        void** new_windows = (void**)realloc(
            vulkan->multi_window.active_windows, sizeof(void*) * total_windows);
        
        if (new_windows) {
            vulkan->multi_window.active_windows = new_windows;
            vulkan->multi_window.window_count = total_windows;
        }
    }
    
    // 这里可以实现更复杂的批处理逻辑
    // 例如按材质分组、按渲染状态分组等
    log_message(COMPOSITOR_LOG_DEBUG, "Prepared for %d windows, %d render batches",
               total_windows, total_windows); // 简单实现：每个窗口一个批次
}

// 清理多窗口管理资源
void cleanup_multi_window_resources(VulkanState* vulkan) {
    if (vulkan->multi_window.active_windows) {
        free(vulkan->multi_window.active_windows);
        vulkan->multi_window.active_windows = NULL;
    }
    vulkan->multi_window.window_count = 0;
    vulkan->multi_window.active_batch_count = 0;
}

// 渲染背景（脏区域版本）
void render_background_dirty(CompositorState* state) {
    if (state->dirty_rect_count == 0) {
        return;
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Rendering background for %d dirty regions", 
               state->dirty_rect_count);
    
    // 只渲染脏区域的背景部分
    for (int i = 0; i < state->dirty_rect_count; i++) {
        DirtyRect* rect = &state->dirty_rects[i];
        log_message(COMPOSITOR_LOG_DEBUG, "Rendering background rect: %d,%d %dx%d",
                   rect->x, rect->y, rect->width, rect->height);
        // 实际代码会针对每个脏区域渲染背景
    }
}

// 检查窗口是否与脏区域相交
bool check_window_intersects_dirty_rect(CompositorState* state, void* window, bool is_wayland) {
    int window_x, window_y, window_width, window_height;
    
    if (is_wayland) {
        WaylandWindow* wl_window = (WaylandWindow*)window;
        window_x = wl_window->x;
        window_y = wl_window->y;
        window_width = wl_window->width;
        window_height = wl_window->height;
    } else {
        XwaylandWindowState* xwl_window = (XwaylandWindowState*)window;
        window_x = xwl_window->x;
        window_y = xwl_window->y;
        window_width = xwl_window->width;
        window_height = xwl_window->height;
    }
    
    // 检查窗口是否与任何脏区域相交
    for (int i = 0; i < state->dirty_rect_count; i++) {
        DirtyRect* rect = &state->dirty_rects[i];
        
        // 矩形相交检测
        bool intersects = !(rect->x > window_x + window_width ||
                           rect->x + rect->width < window_x ||
                           rect->y > window_y + window_height ||
                           rect->y + rect->height < window_y);
        
        if (intersects) {
            return true;
        }
    }
    
    return false;
}

// 获取渲染性能统计
void get_render_performance_stats(CompositorState* state, RenderPerfStats* stats) {
    if (!state || !stats) {
        return;
    }
    
    RenderPerfMonitor* monitor = &state->vulkan.perf_monitor;
    
    stats->fps = monitor->fps;
    stats->frame_count = monitor->frame_count;
    stats->avg_frame_time = monitor->avg_frame_time;
    stats->render_time = monitor->render_time;
    stats->image_acquire_time = monitor->image_acquire_time;
    stats->submit_time = monitor->submit_time;
    stats->windows_rendered = monitor->windows_rendered;
    stats->total_render_time = monitor->total_frame_time;
}

// 获取内存使用统计
void get_vulkan_memory_stats(CompositorState* state, VulkanMemStats* stats) {
    if (!state || !stats) {
        return;
    }
    
    VulkanMemStats* vulkan_stats = &state->vulkan.mem_stats;
    *stats = *vulkan_stats;
}

// 启用/禁用渲染优化
void set_render_optimization_enabled(CompositorState* state, bool enabled) {
    if (!state) {
        return;
    }
    
    state->vulkan.render_optimization.enabled = enabled;
    log_message(COMPOSITOR_LOG_INFO, "Render optimization %s", enabled ? "enabled" : "disabled");
}

// 异步上传纹理到GPU
int upload_texture_async(VulkanState* vulkan, void* texture_data, size_t data_size, TextureUploadCallback callback, void* user_data) {
    log_message(COMPOSITOR_LOG_DEBUG, "Scheduling asynchronous texture upload, size: %.2f KB",
               data_size / 1024.0f);
    
    // 实际代码会使用传输命令池创建传输命令，并在单独的线程或队列中执行
    // 这里简化处理，直接返回成功
    return COMPOSITOR_OK;
}