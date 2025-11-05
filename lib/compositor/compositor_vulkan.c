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
    state->vulkan.render_optimization.use_render_batching = true;      // 启用渲染批次管理
    state->vulkan.render_optimization.use_render_queue = true;        // 启用渲染队列
    state->vulkan.render_optimization.use_instanced_rendering = true; // 启用实例化渲染
    state->vulkan.render_optimization.use_adaptive_sync = true;       // 启用自适应同步
    state->vulkan.render_optimization.max_anisotropy = 16.0f;         // 设置最大各向异性过滤
    
    // 初始化多窗口管理
    state->vulkan.multi_window.active_batch_count = 0;
    state->vulkan.multi_window.window_count = 0;
    state->vulkan.multi_window.active_windows = NULL;
    
    // 初始化渲染批次
    state->vulkan.render_batches = NULL;
    state->vulkan.render_batch_count = 0;
    state->vulkan.render_batch_capacity = 64;
    
    // 初始化渲染队列
    state->vulkan.render_queue = NULL;
    state->vulkan.render_queue_size = 0;
    state->vulkan.render_queue_capacity = 1024;
    
    // 初始化性能统计
    state->vulkan.perf_stats.frame_time = 0.0f;
    state->vulkan.perf_stats.draw_calls = 0;
    state->vulkan.perf_stats.batch_count = 0;
    state->vulkan.perf_stats.surface_count = 0;
    state->vulkan.perf_stats.texture_switches = 0;
    
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
    
    // 添加优化字段
    vulkan->texture_cache.hit_count = 0;
    vulkan->texture_cache.miss_count = 0;
    vulkan->texture_cache.eviction_count = 0;
    vulkan->texture_cache.last_maintenance_time = 0;
    vulkan->texture_cache.memory_pressure_level = 0.0f; // 0.0-1.0, 1.0表示压力大
    
    // 检测支持的纹理压缩格式
    vulkan->texture_cache.supported_compression_formats = detect_texture_compression_formats(vulkan);
    
    // 分配初始容量 - 使用更合理的初始大小
    vulkan->texture_cache.capacity = 128; // 初始可存储128个纹理
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
    
    // 初始化渲染批次管理
    if (init_render_batches(vulkan) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_WARNING, "Failed to initialize render batches");
        // 继续执行，这不是致命错误
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Texture cache initialized with compression support: %s",
               vulkan->texture_cache.supported_compression_formats ? "yes" : "no");
    
    return COMPOSITOR_OK;
}

// 清理纹理缓存
void cleanup_texture_cache(VulkanState* vulkan) {
    if (!vulkan->texture_cache.textures) {
        return;
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Cleaning up texture cache, %d textures to release",
               vulkan->texture_cache.texture_count);
    
    // 记录缓存最终统计
    float hit_rate = 0.0f;
    if (vulkan->texture_cache.hit_count + vulkan->texture_cache.miss_count > 0) {
        hit_rate = (float)vulkan->texture_cache.hit_count / 
                  (vulkan->texture_cache.hit_count + vulkan->texture_cache.miss_count);
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Texture cache final stats: Hit rate: %.1f%%, Evictions: %d",
               hit_rate * 100.0f, vulkan->texture_cache.eviction_count);
    
    // 释放所有纹理资源
    for (int i = 0; i < vulkan->texture_cache.texture_count; i++) {
        TextureCacheEntry* entry = &vulkan->texture_cache.textures[i];
        if (entry->texture) {
            // 实际代码会释放Vulkan纹理资源
            log_message(COMPOSITOR_LOG_DEBUG, "Releasing cached texture: %s", entry->name ? entry->name : "unnamed");
            
            // 减少内存使用统计
            vulkan->mem_stats.texture_memory -= entry->size_bytes;
            vulkan->mem_stats.total_allocated -= entry->size_bytes;
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
    
    log_message(COMPOSITOR_LOG_DEBUG, "Texture cache cleaned up completely");
}

// 检测纹理压缩格式支持
static uint32_t detect_texture_compression_formats(VulkanState* vulkan) {
    // 在实际实现中，这里会检查物理设备支持的纹理压缩格式
    // 例如VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK等
    // 这里简化处理，假设支持基本的压缩格式
    return 1; // 返回1表示支持压缩
}

// 执行纹理缓存维护
static void perform_texture_cache_maintenance(VulkanState* vulkan) {
    uint64_t current_time = get_current_time_ms();
    
    // 每1000毫秒（1秒）执行一次维护
    if (current_time - vulkan->texture_cache.last_maintenance_time < 1000) {
        return;
    }
    
    vulkan->texture_cache.last_maintenance_time = current_time;
    
    // 计算内存压力级别
    vulkan->texture_cache.memory_pressure_level = 
        (float)vulkan->texture_cache.current_size_bytes / (float)vulkan->texture_cache.max_size_bytes;
    
    // 记录缓存统计信息
    if (vulkan->texture_cache.hit_count + vulkan->texture_cache.miss_count > 0) {
        float hit_rate = (float)vulkan->texture_cache.hit_count / 
                        (vulkan->texture_cache.hit_count + vulkan->texture_cache.miss_count);
        
        log_message(COMPOSITOR_LOG_DEBUG, "Texture cache stats - Hit rate: %.1f%%, Size: %.2f/%.2f MB, Pressure: %.1f%%",
                   hit_rate * 100.0f,
                   vulkan->texture_cache.current_size_bytes / (1024.0f * 1024.0f),
                   vulkan->texture_cache.max_size_bytes / (1024.0f * 1024.0f),
                   vulkan->texture_cache.memory_pressure_level * 100.0f);
    }
    
    // 如果内存压力高，提前清理一些不常用的纹理
    if (vulkan->texture_cache.memory_pressure_level > 0.7f && vulkan->texture_cache.texture_count > 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "High memory pressure, performing early texture eviction");
        
        // 清理最近5秒内未使用的纹理
        uint64_t five_seconds_ago = current_time - 5000;
        int evicted_count = 0;
        
        // 从后向前遍历，避免索引问题
        for (int i = vulkan->texture_cache.texture_count - 1; i >= 0; i--) {
            TextureCacheEntry* entry = &vulkan->texture_cache.textures[i];
            if (entry->last_used < five_seconds_ago) {
                // 释放纹理资源
                if (entry->texture) {
                    // 实际代码会释放Vulkan纹理资源
                    log_message(COMPOSITOR_LOG_DEBUG, "Early eviction of texture: %s", 
                               entry->name ? entry->name : "unnamed");
                }
                if (entry->name) {
                    free((void*)entry->name);
                }
                
                // 更新缓存统计
                vulkan->texture_cache.current_size_bytes -= entry->size_bytes;
                vulkan->mem_stats.texture_memory -= entry->size_bytes;
                
                // 移除条目（将最后一个条目移到当前位置）
                if (i < vulkan->texture_cache.texture_count - 1) {
                    vulkan->texture_cache.textures[i] = 
                        vulkan->texture_cache.textures[vulkan->texture_cache.texture_count - 1];
                }
                
                vulkan->texture_cache.texture_count--;
                vulkan->texture_cache.eviction_count++;
                evicted_count++;
                
                // 最多提前清理10个纹理
                if (evicted_count >= 10) {
                    break;
                }
            }
        }
        
        if (evicted_count > 0) {
            log_message(COMPOSITOR_LOG_DEBUG, "Early eviction completed, removed %d textures", evicted_count);
        }
    }
}

// 智能LRU策略 - 考虑纹理大小和使用频率
static int find_texture_to_evict(VulkanState* vulkan) {
    if (vulkan->texture_cache.texture_count == 0) {
        return -1;
    }
    
    int64_t current_time = get_current_time_ms();
    float worst_score = 0.0f;
    int worst_index = 0;
    
    for (int i = 0; i < vulkan->texture_cache.texture_count; i++) {
        TextureCacheEntry* entry = &vulkan->texture_cache.textures[i];
        
        // 计算LRU分数：
        // 1. 时间因子：越久未使用，分数越高
        float time_factor = (float)(current_time - entry->last_used) / 1000.0f; // 转换为秒
        
        // 2. 大小因子：越大的纹理，分数越高
        float size_factor = (float)entry->size_bytes / 1024.0f; // 转换为KB
        
        // 3. 调整分数 - 对于大纹理给予更高权重
        float score = time_factor * (1.0f + (size_factor / 1024.0f)); // 对于超过1MB的纹理给予额外权重
        
        if (i == 0 || score > worst_score) {
            worst_score = score;
            worst_index = i;
        }
    }
    
    return worst_index;
}

// 缓存纹理 - 优化版本
int cache_texture(VulkanState* vulkan, const char* name, void* texture, size_t size_bytes) {
    // 执行缓存维护
    perform_texture_cache_maintenance(vulkan);
    
    // 快速查找优化 - 先检查名称是否为NULL
    if (name) {
        // 查找是否已存在
        for (int i = 0; i < vulkan->texture_cache.texture_count; i++) {
            if (vulkan->texture_cache.textures[i].name && 
                strcmp(vulkan->texture_cache.textures[i].name, name) == 0) {
                // 更新现有条目
                vulkan->texture_cache.textures[i].last_used = get_current_time_ms();
                vulkan->texture_cache.textures[i].texture = texture;
                vulkan->texture_cache.textures[i].size_bytes = size_bytes;
                
                // 更新命中计数
                vulkan->texture_cache.hit_count++;
                
                return COMPOSITOR_OK;
            }
        }
    }
    
    // 未找到，记录未命中
    vulkan->texture_cache.miss_count++;
    
    // 检查是否需要扩容
    if (vulkan->texture_cache.texture_count >= vulkan->texture_cache.capacity) {
        // 使用指数增长，但设置上限
        int new_capacity = vulkan->texture_cache.capacity * 2;
        if (new_capacity > 1024) { // 最大容量1024
            new_capacity = 1024;
        }
        
        if (new_capacity <= vulkan->texture_cache.capacity) {
            // 已达到最大容量，需要先释放一些资源
            log_message(COMPOSITOR_LOG_WARNING, "Texture cache reached maximum capacity");
        } else {
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
    }
    
    // 检查是否需要释放旧纹理以腾出空间
    // 使用更智能的策略，确保新纹理有足够空间
    size_t required_space = size_bytes;
    // 留出一些额外空间以避免频繁清理
    size_t safety_margin = vulkan->texture_cache.max_size_bytes / 20; // 5%的安全余量
    
    while (vulkan->texture_cache.current_size_bytes + required_space > vulkan->texture_cache.max_size_bytes - safety_margin &&
           vulkan->texture_cache.texture_count > 0) {
        // 使用智能LRU策略查找要驱逐的纹理
        int evict_index = find_texture_to_evict(vulkan);
        if (evict_index < 0) {
            break;
        }
        
        // 释放选定的纹理
        TextureCacheEntry* entry = &vulkan->texture_cache.textures[evict_index];
        if (entry->texture) {
            // 实际代码会释放Vulkan纹理资源
            log_message(COMPOSITOR_LOG_DEBUG, "Evicting cached texture: %s, size: %.2f KB", 
                       entry->name ? entry->name : "unnamed", 
                       entry->size_bytes / 1024.0f);
        }
        if (entry->name) {
            free((void*)entry->name);
        }
        
        // 更新缓存统计
        vulkan->texture_cache.current_size_bytes -= entry->size_bytes;
        vulkan->mem_stats.texture_memory -= entry->size_bytes;
        
        // 移除条目（将最后一个条目移到当前位置）
        if (evict_index < vulkan->texture_cache.texture_count - 1) {
            vulkan->texture_cache.textures[evict_index] = 
                vulkan->texture_cache.textures[vulkan->texture_cache.texture_count - 1];
        }
        vulkan->texture_cache.texture_count--;
        vulkan->texture_cache.eviction_count++;
    }
    
    // 添加新纹理
    TextureCacheEntry* new_entry = &vulkan->texture_cache.textures[vulkan->texture_cache.texture_count++];
    new_entry->texture = texture;
    new_entry->size_bytes = size_bytes;
    new_entry->last_used = get_current_time_ms();
    new_entry->name = name ? strdup(name) : NULL;
    
    // 如果支持纹理压缩且纹理大小超过阈值，尝试压缩
    if (vulkan->texture_cache.supported_compression_formats && size_bytes > 1024 * 1024) { // 超过1MB的纹理考虑压缩
        // 实际实现中，这里会尝试纹理压缩
        // compressed_size = compress_texture(vulkan, new_entry);
        // if (compressed_size > 0 && compressed_size < size_bytes) {
        //     new_entry->size_bytes = compressed_size;
        //     vulkan->texture_cache.current_size_bytes -= (size_bytes - compressed_size);
        //     vulkan->mem_stats.texture_memory -= (size_bytes - compressed_size);
        //     log_message(COMPOSITOR_LOG_DEBUG, "Texture compressed from %.2f KB to %.2f KB", 
        //                size_bytes / 1024.0f, compressed_size / 1024.0f);
        // }
    }
    
    // 更新缓存统计
    vulkan->texture_cache.current_size_bytes += new_entry->size_bytes;
    vulkan->mem_stats.texture_memory += new_entry->size_bytes;
    
    // 更新总内存分配
    vulkan->mem_stats.total_allocated += new_entry->size_bytes;
    
    // 更新峰值内存使用
    if (vulkan->mem_stats.total_allocated > vulkan->mem_stats.peak_allocated) {
        vulkan->mem_stats.peak_allocated = vulkan->mem_stats.total_allocated;
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Cached texture: %s, size: %.2f KB, Cache usage: %.1f%%",
               name ? name : "unnamed", 
               new_entry->size_bytes / 1024.0f,
               (float)vulkan->texture_cache.current_size_bytes / vulkan->texture_cache.max_size_bytes * 100.0f);
    
    return COMPOSITOR_OK;
}

// 获取缓存的纹理（基于名称）- 优化版本
void* get_cached_texture(VulkanState* vulkan, const char* name) {
    if (!name) {
        return NULL;
    }
    
    // 简单优化：优先检查最近使用的纹理（前几个）
    // 大多数场景下，最近使用的纹理更容易被再次访问
    int quick_check_count = 5; // 快速检查前5个
    for (int i = 0; i < quick_check_count && i < vulkan->texture_cache.texture_count; i++) {
        if (vulkan->texture_cache.textures[i].name && 
            strcmp(vulkan->texture_cache.textures[i].name, name) == 0) {
            // 更新使用时间
            vulkan->texture_cache.textures[i].last_used = get_current_time_ms();
            vulkan->texture_cache.hit_count++;
            return vulkan->texture_cache.textures[i].texture;
        }
    }
    
    // 如果快速检查失败，遍历整个缓存
    for (int i = quick_check_count; i < vulkan->texture_cache.texture_count; i++) {
        if (vulkan->texture_cache.textures[i].name && 
            strcmp(vulkan->texture_cache.textures[i].name, name) == 0) {
            // 更新使用时间
            vulkan->texture_cache.textures[i].last_used = get_current_time_ms();
            vulkan->texture_cache.hit_count++;
            
            // 将找到的纹理移到前面，加速下次访问
            if (i > 0) {
                TextureCacheEntry temp = vulkan->texture_cache.textures[i];
                // 向前移动其他条目
                memmove(&vulkan->texture_cache.textures[1], &vulkan->texture_cache.textures[0], 
                       i * sizeof(TextureCacheEntry));
                // 将找到的条目放在最前面
                vulkan->texture_cache.textures[0] = temp;
            }
            
            return vulkan->texture_cache.textures[0].texture;
        }
    }
    
    // 未找到，记录未命中
    vulkan->texture_cache.miss_count++;
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

// 初始化渲染批次管理
int init_render_batches(VulkanState* vulkan) {
    if (!vulkan) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 为渲染批次分配初始内存
    vulkan->render_batches = (RenderBatch*)safe_malloc(
        sizeof(RenderBatch) * vulkan->render_batch_capacity);
    if (!vulkan->render_batches) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for render batches");
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    // 初始化每个渲染批次
    for (int i = 0; i < vulkan->render_batch_capacity; i++) {
        RenderBatch* batch = &vulkan->render_batches[i];
        batch->texture_id = -1;
        batch->instance_count = 0;
        batch->instance_capacity = 256;
        batch->instances = (RenderInstance*)safe_malloc(
            sizeof(RenderInstance) * batch->instance_capacity);
        if (!batch->instances) {
            // 清理已分配的资源
            for (int j = 0; j < i; j++) {
                safe_free(vulkan->render_batches[j].instances);
            }
            safe_free(vulkan->render_batches);
            vulkan->render_batches = NULL;
            log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for batch instances");
            return COMPOSITOR_ERROR_MEMORY;
        }
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Render batches initialized with capacity for %d batches", 
            vulkan->render_batch_capacity);
    return COMPOSITOR_OK;
}

// 初始化渲染队列
int init_render_queue(VulkanState* vulkan) {
    if (!vulkan) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 为渲染队列分配内存
    vulkan->render_queue = (RenderCommand*)safe_malloc(
        sizeof(RenderCommand) * vulkan->render_queue_capacity);
    if (!vulkan->render_queue) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for render queue");
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Render queue initialized with capacity for %d commands", 
            vulkan->render_queue_capacity);
    return COMPOSITOR_OK;
}

// 添加渲染命令到队列
int add_render_command(VulkanState* vulkan, RenderCommandType type, void* data) {
    if (!vulkan || !vulkan->render_queue) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 检查队列容量并扩展
    if (vulkan->render_queue_size >= vulkan->render_queue_capacity) {
        int new_capacity = vulkan->render_queue_capacity * 2;
        RenderCommand* new_queue = (RenderCommand*)safe_realloc(
            vulkan->render_queue, sizeof(RenderCommand) * new_capacity);
        if (!new_queue) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to resize render queue");
            return COMPOSITOR_ERROR_MEMORY;
        }
        vulkan->render_queue = new_queue;
        vulkan->render_queue_capacity = new_capacity;
        log_message(COMPOSITOR_LOG_INFO, "Render queue resized to %d commands", new_capacity);
    }
    
    // 添加命令到队列
    RenderCommand* command = &vulkan->render_queue[vulkan->render_queue_size++];
    command->type = type;
    command->data = data;
    command->timestamp = get_current_time_ms();
    
    return COMPOSITOR_OK;
}

// 优化渲染批次
int optimize_render_batches(VulkanState* vulkan) {
    if (!vulkan || !vulkan->render_optimization.use_render_batching) {
        return COMPOSITOR_OK;
    }
    
    // 记录优化前的批次数量
    int before_count = vulkan->render_batch_count;
    int optimized_count = 0;
    
    // 按纹理ID排序批次，减少纹理切换
    // 合并相同纹理的批次
    // 移除空批次
    
    // 实际优化逻辑将在这里实现
    
    vulkan->perf_stats.batch_count = vulkan->render_batch_count;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Optimized render batches: %d -> %d (merged %d)",
            before_count, vulkan->render_batch_count, before_count - vulkan->render_batch_count);
    
    return COMPOSITOR_OK;
}

// 执行渲染队列
int execute_render_queue(VulkanState* vulkan, VkCommandBuffer command_buffer) {
    if (!vulkan || !vulkan->render_queue || !command_buffer) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Executing render queue with %d commands", 
            vulkan->render_queue_size);
    
    // 优化渲染批次
    optimize_render_batches(vulkan);
    
    // 重置性能统计
    vulkan->perf_stats.draw_calls = 0;
    vulkan->perf_stats.texture_switches = 0;
    
    // 实际执行渲染命令的逻辑将在这里实现
    // 遍历渲染队列，执行每个命令
    
    // 重置队列
    vulkan->render_queue_size = 0;
    
    return COMPOSITOR_OK;
}

// 更新性能统计信息
void update_vulkan_performance_stats(VulkanState* vulkan) {
    if (!vulkan) return;
    
    // 更新帧计数
    vulkan->perf_monitor.frame_count++;
    
    // 计算FPS
    uint64_t current_time = get_current_time_ms();
    uint64_t elapsed_time = current_time - vulkan->perf_monitor.start_time;
    
    if (elapsed_time >= 1000) {  // 每秒更新一次
        vulkan->perf_monitor.fps = vulkan->perf_monitor.frame_count * 1000 / elapsed_time;
        vulkan->perf_monitor.frame_count = 0;
        vulkan->perf_monitor.start_time = current_time;
        
        // 记录性能指标
        log_message(COMPOSITOR_LOG_DEBUG, "Performance stats - FPS: %d, DrawCalls: %d, Batches: %d, Surfaces: %d",
                vulkan->perf_monitor.fps,
                vulkan->perf_stats.draw_calls,
                vulkan->perf_stats.batch_count,
                vulkan->perf_stats.surface_count);
        
        // 检查内存使用峰值
        if (vulkan->mem_stats.total_allocated > vulkan->mem_stats.peak_allocated) {
            vulkan->mem_stats.peak_allocated = vulkan->mem_stats.total_allocated;
            log_message(COMPOSITOR_LOG_DEBUG, "Memory usage peak: %.2f MB", 
                    vulkan->mem_stats.peak_allocated / (1024.0 * 1024.0));
        }
    }
}

// 实现高效的窗口合成渲染
// 静态辅助函数：快速排序实现窗口Z顺序排序
static void quick_sort_windows(void** windows, bool* types, int* z_orders, int left, int right) {
    if (left < right) {
        int pivot = z_orders[right];
        int i = left - 1;
        
        for (int j = left; j < right; j++) {
            if (z_orders[j] <= pivot) {
                i++;
                // 交换窗口
                void* temp_window = windows[i];
                windows[i] = windows[j];
                windows[j] = temp_window;
                
                // 交换类型
                bool temp_type = types[i];
                types[i] = types[j];
                types[j] = temp_type;
                
                // 交换Z顺序
                int temp_z = z_orders[i];
                z_orders[i] = z_orders[j];
                z_orders[j] = temp_z;
            }
        }
        
        // 交换pivot
        void* temp_window = windows[i + 1];
        windows[i + 1] = windows[right];
        windows[right] = temp_window;
        
        bool temp_type = types[i + 1];
        types[i + 1] = types[right];
        types[right] = temp_type;
        
        int temp_z = z_orders[i + 1];
        z_orders[i + 1] = z_orders[right];
        z_orders[right] = temp_z;
        
        int pivot_index = i + 1;
        quick_sort_windows(windows, types, z_orders, left, pivot_index - 1);
        quick_sort_windows(windows, types, z_orders, pivot_index + 1, right);
    }
}

// 静态辅助函数：收集和准备可见窗口
static int collect_and_prepare_visible_windows(CompositorState* state, 
                                             void*** out_windows, 
                                             bool** out_types, 
                                             int** out_z_orders) {
    VulkanState* vulkan = &state->vulkan;
    int visible_count = 0;
    
    // 预先计算可见窗口数量
    for (int i = 0; i < state->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = state->xwayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == state->active_workspace || window->is_sticky) &&
            !window->is_hidden) {
            visible_count++;
        }
    }
    
    for (int i = 0; i < state->wayland_state.window_count; i++) {
        WaylandWindow* window = state->wayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == state->active_workspace || window->is_sticky) &&
            !window->is_hidden) {
            visible_count++;
        }
    }
    
    // 没有可见窗口，提前返回
    if (visible_count == 0) {
        *out_windows = NULL;
        *out_types = NULL;
        *out_z_orders = NULL;
        return 0;
    }
    
    // 分配内存
    void** windows = (void**)malloc(visible_count * sizeof(void*));
    bool* types = (bool*)malloc(visible_count * sizeof(bool));
    int* z_orders = (int*)malloc(visible_count * sizeof(int));
    
    if (!windows || !types || !z_orders) {
        free(windows);
        free(types);
        free(z_orders);
        return -1;
    }
    
    // 填充数据
    int index = 0;
    
    // 添加Xwayland窗口
    for (int i = 0; i < state->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = state->xwayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == state->active_workspace || window->is_sticky) &&
            !window->is_hidden) {
            
            windows[index] = window;
            types[index] = false; // Xwayland窗口
            z_orders[index] = window->z_order;
            
            // 记录窗口最后渲染时间（用于纹理缓存管理）
            window->last_render_time = get_current_time_ms();
            index++;
        }
    }
    
    // 添加Wayland窗口
    for (int i = 0; i < state->wayland_state.window_count; i++) {
        WaylandWindow* window = state->wayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == state->active_workspace || window->is_sticky) &&
            !window->is_hidden) {
            
            windows[index] = window;
            types[index] = true; // Wayland窗口
            z_orders[index] = window->z_order;
            
            // 记录窗口最后渲染时间
            window->last_render_time = get_current_time_ms();
            index++;
        }
    }
    
    // 使用快速排序替代冒泡排序，提高大量窗口时的性能
    if (visible_count > 1) {
        quick_sort_windows(windows, types, z_orders, 0, visible_count - 1);
    }
    
    *out_windows = windows;
    *out_types = types;
    *out_z_orders = z_orders;
    return visible_count;
}

int render_windows_with_hardware_acceleration(CompositorState* state) {
    if (!state || !state->vulkan.device) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    VulkanState* vulkan = &state->vulkan;
    uint64_t start_time = get_current_time_ms();
    
    // 清空渲染队列和性能统计
    vulkan->render_queue_size = 0;
    vulkan->perf_stats.surface_count = 0;
    vulkan->perf_stats.draw_calls = 0;
    vulkan->perf_stats.batch_count = 0;
    vulkan->perf_stats.texture_switches = 0;
    vulkan->perf_stats.culled_windows = 0;
    vulkan->perf_stats.occluded_windows = 0;
    vulkan->perf_stats.batch_optimization_gain = 0;
    
    // 性能优化：提前检查是否需要渲染
    if (state->xwayland_state.window_count == 0 && state->wayland_state.window_count == 0) {
        vulkan->perf_stats.frame_time = get_current_time_ms() - start_time;
        return COMPOSITOR_OK;
    }
    
    // 1. 进行可见性剔除
    if (state->config.use_window_culling) {
        cull_invisible_windows(state);
    }
    
    // 2. 合并脏区域以优化渲染
    if (state->config.use_dirty_rect_optimization && state->dirty_rect_count > 0) {
        merge_dirty_rects(state);
    }
    
    // 3. 收集可见窗口并按Z顺序排序（使用优化的辅助函数）
    void** visible_windows = NULL;
    bool* window_types = NULL;
    int* z_orders = NULL;
    int visible_count = collect_and_prepare_visible_windows(state, &visible_windows, &window_types, &z_orders);
    
    // 如果有可见窗口，继续处理
    if (visible_count > 0) {
        // 4. 执行窗口遮挡剔除（优化大量重叠窗口的场景）
        if (state->config.use_occlusion_culling && visible_count > 5) {
            // 对已排序的窗口（从后向前）进行遮挡检测
            // 已经按Z顺序排序，从后向前检查，后面的窗口可能遮挡前面的窗口
            for (int i = visible_count - 1; i >= 0; i--) {
                bool is_occluded = false;
                
                // 检查当前窗口是否被后面的窗口完全遮挡
                for (int j = i + 1; j < visible_count && !is_occluded; j++) {
                    if (is_window_completely_occluded(visible_windows[i], window_types[i], 
                                                    visible_windows[j], window_types[j])) {
                        is_occluded = true;
                        vulkan->perf_stats.occluded_windows++;
                    }
                }
                
                // 如果窗口被完全遮挡，可以跳过渲染
                if (is_occluded) {
                    // 标记窗口为隐藏，避免后续渲染
                    if (window_types[i]) {
                        ((WaylandWindow*)visible_windows[i])->is_hidden = true;
                    } else {
                        ((XwaylandWindowState*)visible_windows[i])->is_hidden = true;
                    }
                }
            }
        }
        
        // 5. 准备渲染命令并进行批处理优化
        // 预分配渲染批次以减少内存分配
        prepare_render_batches(state);
        
        // 按纹理类型和渲染属性进行分组，优化批处理
        if (state->config.use_render_batching) {
            // 先按窗口类型和纹理进行分组，再添加到渲染队列
            batch_windows_by_texture(state, visible_windows, window_types, visible_count);
        } else {
            // 不使用批处理时，直接添加每个窗口
            for (int i = 0; i < visible_count; i++) {
                bool use_dirty_rects = false;
                bool is_dirty = false;
                
                // 检查窗口是否需要脏区域优化
                if (state->config.use_dirty_rect_optimization) {
                    is_dirty = check_window_intersects_dirty_rect(state, visible_windows[i], window_types[i]);
                    use_dirty_rects = is_dirty;
                }
                
                // 跳过隐藏的窗口
                if (window_types[i] && ((WaylandWindow*)visible_windows[i])->is_hidden) continue;
                if (!window_types[i] && ((XwaylandWindowState*)visible_windows[i])->is_hidden) continue;
                
                // 准备窗口渲染
                if (prepare_window_rendering(state, visible_windows[i], window_types[i]) != COMPOSITOR_OK) {
                    log_message(COMPOSITOR_LOG_WARNING, "Failed to prepare window for rendering");
                    continue;
                }
                
                // 添加窗口渲染命令到队列
                if (window_types[i]) {
                    add_render_command(vulkan, RENDER_COMMAND_WINDOW, visible_windows[i]);
                } else {
                    add_render_command(vulkan, RENDER_COMMAND_XWAYLAND_WINDOW, visible_windows[i]);
                }
                
                vulkan->perf_stats.surface_count++;
            }
        }
        
        // 6. 优化渲染批次
        if (vulkan->render_queue_size > 0) {
            int before_batch_count = vulkan->perf_stats.batch_count;
            optimize_render_batches(vulkan);
            vulkan->perf_stats.batch_optimization_gain = before_batch_count - vulkan->perf_stats.batch_count;
        }
        
        // 7. 执行渲染队列
        if (vulkan->render_queue_size > 0) {
            // 获取当前命令缓冲区
            uint32_t current_frame = vulkan->current_frame % MAX_FRAMES_IN_FLIGHT;
            VkCommandBuffer command_buffer = vulkan->command_buffers[current_frame];
            
            // 重置命令缓冲区
            VkResult result = vkResetCommandBuffer(command_buffer, 0);
            if (result != VK_SUCCESS) {
                log_message(COMPOSITOR_LOG_ERROR, "Failed to reset command buffer");
                free(visible_windows);
                free(window_types);
                free(z_orders);
                return COMPOSITOR_ERROR_VULKAN;
            }
            
            // 开始记录命令
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            // 使用多次提交标志优化性能
            begin_info.flags = state->config.use_command_buffer_reuse ? 
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT : 
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            
            if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
                log_message(COMPOSITOR_LOG_ERROR, "Failed to begin command buffer");
                free(visible_windows);
                free(window_types);
                free(z_orders);
                return COMPOSITOR_ERROR_VULKAN;
            }
            
            // 执行渲染队列
            execute_render_queue(vulkan, command_buffer);
            
            // 结束记录命令
            if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
                log_message(COMPOSITOR_LOG_ERROR, "Failed to end command buffer");
                free(visible_windows);
                free(window_types);
                free(z_orders);
                return COMPOSITOR_ERROR_VULKAN;
            }
            
            // 记录命令缓冲区提交时间
            vulkan->last_command_buffer_submit = get_current_time_ms();
        }
        
        // 释放临时内存
        free(visible_windows);
        free(window_types);
        free(z_orders);
    }
    
    // 8. 清理完成的窗口渲染资源
    // 优化：只清理实际渲染过的窗口
    for (int i = 0; i < state->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = state->xwayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == state->active_workspace || window->is_sticky)) {
            
            finish_window_rendering(state, window, false);
            // 重置隐藏标志，为下一帧做准备
            window->is_hidden = false;
        }
    }
    
    for (int i = 0; i < state->wayland_state.window_count; i++) {
        WaylandWindow* window = state->wayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == state->active_workspace || window->is_sticky)) {
            
            finish_window_rendering(state, window, true);
            // 重置隐藏标志
            window->is_hidden = false;
        }
    }
    
    // 9. 纹理缓存维护
    if (state->config.use_texture_caching && 
        (get_current_time_ms() - vulkan->last_texture_cache_maintenance) > TEXTURE_CACHE_MAINTENANCE_INTERVAL) {
        perform_texture_cache_maintenance(vulkan);
        vulkan->last_texture_cache_maintenance = get_current_time_ms();
    }
    
    // 计算帧时间
    vulkan->perf_stats.frame_time = get_current_time_ms() - start_time;
    
    // 动态调整渲染优化设置
    if (state->config.use_adaptive_rendering) {
        adapt_rendering_quality(vulkan);
    }
    
    // 更新性能统计
    update_vulkan_performance_stats(vulkan);
    
    // 清除脏区域标记
    if (state->config.use_dirty_rect_optimization) {
        compositor_clear_dirty_rects();
    }
    
    return COMPOSITOR_OK;
}