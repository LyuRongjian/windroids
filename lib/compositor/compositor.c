/*
 * WinDroids Compositor
 * Main implementation file containing core functionality and module integration
 */

#include "compositor.h" // 通过compositor.h间接包含其他头文件
#include <android/native_window.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "compositor_input.h" // 包含输入处理头文件
#include "compositor_utils.h" // 包含工具函数头文件
#include "compositor_render.h" // 包含渲染模块头文件
#include "compositor_dirty.h" // 包含脏区域管理头文件

// 全局状态
static CompositorState g_compositor_state;
static bool g_initialized = false;

// Schedule a redraw of the compositor
void compositor_schedule_redraw(void) {
    // This function has been moved to compositor_render.c
}

// Mark a rect as dirty for redrawing
void compositor_mark_dirty_rect(int x, int y, int width, int height) {
    // This function has been moved to compositor_dirty.c
}

// Clear all dirty rects
void compositor_clear_dirty_rects(void) {
    // This function has been moved to compositor_dirty.c
}

// Merge overlapping dirty rects
void merge_dirty_rects(void) {
    // This function has been moved to compositor_dirty.c
}

// 初始化合成器
int compositor_init(ANativeWindow* window, int width, int height, CompositorConfig* config) {
    if (g_initialized) {
        log_message(COMPOSITOR_LOG_WARN, "Compositor already initialized");
        return COMPOSITOR_OK;
    }
    
    if (!window) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid window handle");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Initializing compositor...");
    
    // 初始化全局状态
    memset(&g_compositor_state, 0, sizeof(CompositorState));
    g_compositor_state.window = window;
    g_compositor_state.width = width;
    g_compositor_state.height = height;
    g_compositor_state.next_z_order = 10;  // 初始Z轴顺序从10开始
    g_compositor_state.use_dirty_rect_optimization = g_compositor_state.config.enable_dirty_rects;
    
    // 合并配置
    g_compositor_state.config = *compositor_merge_config(config);
    
    // 验证配置
    if (compositor_validate_config(&g_compositor_state.config) != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid compositor configuration");
        return COMPOSITOR_ERROR_INVALID_CONFIG;
    }
    
    // 打印配置信息
    if (g_compositor_state.config.debug_mode) {
        compositor_print_config(&g_compositor_state.config);
    }
    
    // 设置日志级别
    utils_set_log_level(g_compositor_state.config.log_level);
    
    // 初始化各模块状态
    compositor_window_set_state(&g_compositor_state);
    compositor_input_set_state(&g_compositor_state);
    compositor_render_set_state(&g_compositor_state);
    compositor_dirty_set_state(&g_compositor_state);
    
    // 设置性能监控模块的状态引用
    compositor_perf_set_state(&g_compositor_state);
    
    // 初始化事件系统
    compositor_events_set_state(&g_compositor_state);
    if (compositor_events_init() != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize event system");
        goto cleanup;
    }
    
    // 初始化性能监控系统
    if (compositor_perf_init() != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize performance monitoring");
        goto cleanup;
    }
    
    // 初始化工具函数系统
    compositor_utils_set_state(&g_compositor_state);
    
    // 初始化Xwayland状态
    memset(&g_compositor_state.xwayland_state, 0, sizeof(XwaylandState));
    g_compositor_state.xwayland_state.windows = NULL;
    g_compositor_state.xwayland_state.window_count = 0;
    g_compositor_state.xwayland_state.max_windows = g_compositor_state.config.max_windows;
    g_compositor_state.xwayland_state.capacity = 0;
    
    // 初始化Wayland状态
    memset(&g_compositor_state.wayland_state, 0, sizeof(WaylandState));
    g_compositor_state.wayland_state.windows = NULL;
    g_compositor_state.wayland_state.window_count = 0;
    g_compositor_state.wayland_state.max_windows = g_compositor_state.config.max_windows;
    g_compositor_state.wayland_state.capacity = 0;
    
    // 初始化拖动状态
    g_compositor_state.dragging_window = NULL;
    g_compositor_state.is_dragging = false;
    g_compositor_state.drag_offset_x = 0;
    g_compositor_state.drag_offset_y = 0;
    g_compositor_state.active_window = NULL;
    g_compositor_state.active_window_is_wayland = false;
    
    // 初始化手势状态
    g_compositor_state.is_gesturing = false;
    g_compositor_state.last_gesture_type = COMPOSITOR_GESTURE_NONE;
    
    // 初始化多窗口管理增强功能
    g_compositor_state.workspaces = NULL;
    g_compositor_state.workspace_count = 0;
    g_compositor_state.active_workspace = 0;
    g_compositor_state.window_groups = NULL;
    g_compositor_state.window_group_count = 0;
    g_compositor_state.tile_mode = TILE_MODE_NONE;
    g_compositor_state.window_snapshots = NULL;
    g_compositor_state.snapshot_is_wayland = NULL;
    g_compositor_state.snapshot_count = 0;
    
    // 创建默认工作区
    compositor_create_workspace("Default");
    
    // 初始化多窗口管理增强功能
    g_compositor_state.workspaces = NULL;
    g_compositor_state.workspace_count = 0;
    g_compositor_state.active_workspace = 0;
    g_compositor_state.window_groups = NULL;
    g_compositor_state.window_group_count = 0;
    g_compositor_state.tile_mode = TILE_MODE_NONE;
    g_compositor_state.window_snapshots = NULL;
    g_compositor_state.snapshot_is_wayland = NULL;
    g_compositor_state.snapshot_count = 0;
    
    // 创建默认工作区
    compositor_create_workspace("Default");
    
    // 初始化脏区域数组
    if (g_compositor_state.config.enable_dirty_rects && g_compositor_state.config.max_dirty_rects > 0) {
        g_compositor_state.dirty_rects = (DirtyRect*)safe_malloc(
            sizeof(DirtyRect) * g_compositor_state.config.max_dirty_rects);
        if (!g_compositor_state.dirty_rects) {
            goto cleanup;
        }
        g_compositor_state.dirty_rect_count = 0;
        g_compositor_state.use_dirty_rect_optimization = true;
        track_memory_allocation(sizeof(DirtyRect) * g_compositor_state.config.max_dirty_rects);
    }
    
    // 分配窗口数组（使用动态扩容机制）
    int initial_capacity = 8;  // 初始容量
    if (g_compositor_state.config.enable_xwayland) {
        g_compositor_state.xwayland_state.windows = (XwaylandWindowState**)safe_malloc(
            sizeof(XwaylandWindowState*) * initial_capacity);
        if (!g_compositor_state.xwayland_state.windows) {
            goto cleanup;
        }
        g_compositor_state.xwayland_state.capacity = initial_capacity;
        track_memory_allocation(sizeof(XwaylandWindowState*) * initial_capacity);
    }
    
    g_compositor_state.wayland_state.windows = (WaylandWindow**)safe_malloc(
        sizeof(WaylandWindow*) * initial_capacity);
    if (!g_compositor_state.wayland_state.windows) {
        goto cleanup;
    }
    g_compositor_state.wayland_state.capacity = initial_capacity;
    track_memory_allocation(sizeof(WaylandWindow*) * initial_capacity);
    
    // 初始化Vulkan
    if (g_compositor_state.config.use_hardware_acceleration) {
        if (init_vulkan(&g_compositor_state) != COMPOSITOR_OK) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize Vulkan");
            goto cleanup;
        }
    }
    
    // 初始化输入系统
    if (compositor_input_init() != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize input system");
        goto cleanup;
    }
    
    // 设置输入捕获模式
    compositor_input_set_capture_mode(g_compositor_state.config.input_capture_mode);
    
    // 初始化触控笔状态
    g_compositor_state.pen_is_pressed = false;
    g_compositor_state.pen_last_x = 0;
    g_compositor_state.pen_last_y = 0;
    g_compositor_state.pen_last_pressure = 0.0f;
    g_compositor_state.pen_last_tilt_x = 0;
    g_compositor_state.pen_last_tilt_y = 0;
    g_compositor_state.pen_pressed_time = 0;
    
    // 初始化输入配置
    g_compositor_state.config.enable_gestures = true;
    g_compositor_state.config.enable_touch_emulation = false;
    g_compositor_state.config.joystick_mouse_emulation = true;
    g_compositor_state.config.joystick_sensitivity = 1.0f;
    g_compositor_state.config.joystick_deadzone = 0.1f;
    g_compositor_state.config.joystick_max_speed = 5;
    g_compositor_state.config.enable_pen_pressure = true;
    g_compositor_state.config.enable_pen_tilt = true;
    g_compositor_state.config.pen_pressure_sensitivity = 1.0f;
    g_compositor_state.config.enable_window_gestures = true;
    g_compositor_state.config.double_tap_timeout = 300;
    g_compositor_state.config.long_press_timeout = 500;
    
    log_message(COMPOSITOR_LOG_INFO, "Input system initialized with multi-device support");
    
    // 设置输入状态指针
    compositor_input_set_state(&g_compositor_state);
    
    // 设置窗口状态指针
    compositor_window_set_state(&g_compositor_state);
    
    // 初始化性能统计
    g_compositor_state.last_frame_time = 0;
    g_compositor_state.fps = 0.0f;
    g_compositor_state.frame_count = 0;
    g_compositor_state.total_render_time = 0;
    g_compositor_state.avg_frame_time = 0.0f;
    
    // 初始化内存跟踪
    g_compositor_state.total_allocated = 0;
    g_compositor_state.peak_allocated = 0;
    
    g_initialized = true;
    log_message(COMPOSITOR_LOG_INFO, "Compositor initialized successfully: %dx%d", width, height);
    return COMPOSITOR_OK;
    
cleanup:
    compositor_destroy();
    return COMPOSITOR_ERROR_INIT;
}

// 从compositor_window.c导入窗口管理函数
// 这些函数现在在compositor_window.c中实现

// 主循环单步
int compositor_step(void) {
    if (!g_initialized) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 开始帧性能测量
    compositor_perf_start_frame();
    
    // 获取当前时间
    int64_t current_time = get_current_time_ms();
    
    // 更新性能统计
    compositor_perf_end_frame();
    compositor_perf_update_stats();
    
    // 在调试模式下生成性能报告（每60帧）
    static int frame_counter = 0;
    if (g_compositor_state.config.debug_mode && (frame_counter % 60 == 0)) {
        char* report = compositor_perf_generate_report();
        if (report) {
            log_message(COMPOSITOR_LOG_INFO, "\n%s", report);
            free(report);
        }
    }
    frame_counter++;
    
    // 实现帧率限制
    if (g_compositor_state.config.max_fps > 0) {
        int64_t frame_time_ms = 1000 / g_compositor_state.config.max_fps;
        int64_t time_since_last_frame = current_time - g_compositor_state.last_frame_time;
        
        if (time_since_last_frame < frame_time_ms) {
            // 帧时间太短，等待以达到目标帧率
            if (g_compositor_state.config.debug_mode) {
                log_message(COMPOSITOR_LOG_DEBUG, "Frame rate limiting: waiting %lld ms", 
                           frame_time_ms - time_since_last_frame);
            }
            
            // 避免完全阻塞，使用非阻塞等待或让出CPU
            utils_sleep_ms(frame_time_ms - time_since_last_frame);
            current_time = get_current_time_ms();
        }
    }
    
    // 处理窗口事件
    process_window_events(&g_compositor_state);
    
    // 处理输入事件 - 通过compositor_input.c中的函数
    
    // 处理输入事件 - 通过compositor_input.c中的函数
    
    // 检查是否需要重绘
    bool needs_redraw = g_compositor_state.needs_redraw || g_compositor_state.config.debug_mode;
    
    // 检查脏区域
    if (g_compositor_state.use_dirty_rect_optimization && g_compositor_state.dirty_rect_count > 0) {
        needs_redraw = true;
    }
    
    if (needs_redraw) {
        // 开始渲染计时
        int64_t render_start_time = current_time;
        
        // 如果启用了脏区域优化，合并脏区域
        if (g_compositor_state.use_dirty_rect_optimization) {
            // 如果有脏区域，合并它们
            if (g_compositor_state.dirty_rect_count > 1) {
                merge_dirty_rects(&g_compositor_state);
            }
            
            // 计算总脏区域大小，决定是否重绘整个屏幕
            if (g_compositor_state.dirty_rect_count > 0) {
                int total_dirty_area = 0;
                for (int i = 0; i < g_compositor_state.dirty_rect_count; i++) {
                    total_dirty_area += (
                        g_compositor_state.dirty_rects[i].width * 
                        g_compositor_state.dirty_rects[i].height);
                }
                
                int screen_area = g_compositor_state.width * g_compositor_state.height;
                
                // 如果脏区域超过屏幕的60%，重绘整个屏幕
                if (total_dirty_area > screen_area * 0.6f) {
                    log_message(COMPOSITOR_LOG_DEBUG, "Dirty area exceeds 60%%, redrawing entire screen");
                    g_compositor_state.dirty_rect_count = 1;
                    g_compositor_state.dirty_rects[0].x = 0;
                    g_compositor_state.dirty_rects[0].y = 0;
                    g_compositor_state.dirty_rects[0].width = g_compositor_state.width;
                    g_compositor_state.dirty_rects[0].height = g_compositor_state.height;
                }
            }
        }
        
        // 渲染帧
    int render_result = COMPOSITOR_OK;
    
    if (g_compositor_state.config.use_hardware_acceleration) {
        // 开始渲染性能测量
        compositor_perf_start_render();
        
        render_result = render_frame();
        
        // 结束渲染性能测量
        compositor_perf_end_render();
    } else {
        // 软件渲染路径（预留）
        log_message(COMPOSITOR_LOG_WARN, "Software rendering not implemented");
        render_result = COMPOSITOR_ERROR_RENDER;
    }
        
        if (render_result != COMPOSITOR_OK) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to render frame: %d", render_result);
            
            // 如果Vulkan渲染失败，尝试回退到软件渲染
            if (render_result == COMPOSITOR_ERROR_VULKAN && 
                g_compositor_state.config.use_hardware_acceleration) {
                log_message(COMPOSITOR_LOG_WARN, "Falling back to software rendering");
                // 实际实现中应该调用软件渲染函数
                log_message(COMPOSITOR_LOG_ERROR, "Software rendering fallback not available");
            }
            
            return render_result;
        }
        
        // 记录渲染时间
        int64_t render_time = get_current_time_ms() - render_start_time;
        
        // 限制高CPU使用率
        if (render_time < 5 && g_compositor_state.config.enable_cpu_throttling) {
            utils_sleep_ms(1); // 短暂睡眠以减少CPU使用
        }
        
        // 清除脏区域
            if (g_compositor_state.use_dirty_rect_optimization) {
                compositor_clear_dirty_rects();
            }
        
        g_compositor_state.needs_redraw = false;
    }
    
    // 更新渲染时间统计
    int64_t render_time = get_current_time_ms() - current_time;
    g_compositor_state.total_render_time += render_time;
    if (g_compositor_state.frame_count > 0) {
        g_compositor_state.avg_frame_time = (float)g_compositor_state.total_render_time / g_compositor_state.frame_count;
    }
    
    // 更新性能统计
    update_performance_stats();
    
    // 更新最后帧时间
    g_compositor_state.last_frame_time = current_time;
    
    return COMPOSITOR_OK;
}

// 输入处理相关函数现在在 compositor_input.c 中实现
    // 简化实现：触摸事件可以类似鼠标事件处理
    // 在实际应用中，需要更复杂的多点触摸处理逻辑
    if (event->touch.type == COMPOSITOR_TOUCH_BEGIN) {
        // 单指触摸开始，相当于鼠标按下
        CompositorInputEvent mouse_event;
        mouse_event.type = COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON;
        mouse_event.mouse_button.x = event->touch.points[0].x;
        mouse_event.mouse_button.y = event->touch.points[0].y;
        mouse_event.mouse_button.button = COMPOSITOR_MOUSE_BUTTON_LEFT;
        mouse_event.mouse_button.pressed = true;
        process_mouse_button_event(&mouse_event);
    } else if (event->touch.type == COMPOSITOR_TOUCH_END) {
        // 触摸结束，相当于鼠标释放
        CompositorInputEvent mouse_event;
        mouse_event.type = COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON;
        mouse_event.mouse_button.pressed = false;
        process_mouse_button_event(&mouse_event);
    } else if (event->touch.type == COMPOSITOR_TOUCH_MOTION) {
        // 触摸移动，相当于鼠标移动
        CompositorInputEvent mouse_event;
        mouse_event.type = COMPOSITOR_INPUT_EVENT_MOUSE_MOTION;
        mouse_event.mouse.x = event->touch.points[0].x;
        mouse_event.mouse.y = event->touch.points[0].y;
        process_mouse_motion_event(&mouse_event);
    }
}

// 处理手势事件
// 输入处理相关函数现在在 compositor_input.c 中实现
// void process_gesture_event(CompositorInputEvent* event) {
    if (!g_compositor_state.config.enable_gestures) return;
    
    g_compositor_state.last_gesture_type = event->gesture.type;
    
    switch (event->gesture.type) {
        case COMPOSITOR_GESTURE_PINCH:
            // 处理缩放手势
            if (g_compositor_state.active_window && 
                g_compositor_state.config.enable_window_gesture_scaling) {
                float scale_factor = event->gesture.scale;
                // 在实际应用中，这里会调整窗口大小
                if (g_compositor_state.config.debug_mode) {
                    log_message(COMPOSITOR_LOG_DEBUG, "Pinch gesture detected, scale: %f", scale_factor);
                }
            }
            break;
            
        case COMPOSITOR_GESTURE_SWIPE:
            // 处理滑动手势
            if (g_compositor_state.config.debug_mode) {
                log_message(COMPOSITOR_LOG_DEBUG, "Swipe gesture detected, direction: %d", 
                           event->gesture.direction);
            }
            break;
            
        case COMPOSITOR_GESTURE_DOUBLE_TAP:
            // 处理双击手势（例如最大化窗口）
            if (g_compositor_state.active_window && 
                g_compositor_state.config.enable_window_double_tap_maximize) {
                if (g_compositor_state.config.debug_mode) {
                    log_message(COMPOSITOR_LOG_DEBUG, "Double tap detected on active window");
                }
                // 在实际应用中，这里会处理窗口最大化逻辑
            }
            break;
            
        default:
            break;
    }
}

// 处理窗口事件
void process_window_events(CompositorState* state) {
    if (!state) return;
    
    // 实际实现中会处理窗口创建、销毁等事件
    log_message(COMPOSITOR_LOG_DEBUG, "Processing window events");
    
    // 这里可以添加窗口事件处理逻辑
    // - 窗口创建/销毁事件
    // - 窗口属性变更事件
    // - 窗口表面更新事件
    // - 窗口焦点变化事件
    
    // 检查窗口是否需要更新
    for (int i = 0; i < state->wayland_state.window_count; i++) {
        WaylandWindow* window = state->wayland_state.windows[i];
        if (window->is_dirty) {
            // 标记窗口区域为脏
            compositor_mark_dirty_rect(window->x, window->y, 
                                     window->width, window->height);
            window->is_dirty = false;
        }
    }
    
    for (int i = 0; i < state->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = state->xwayland_state.windows[i];
        if (window->is_dirty) {
            // 标记窗口区域为脏
            compositor_mark_dirty_rect(window->x, window->y, 
                                     window->width, window->height);
            window->is_dirty = false;
        }
    }
}

// 合并脏区域 - 现在在compositor_dirty.c中实现

// 销毁合成器
void compositor_destroy(void) {
    if (!g_initialized) return;
    
    log_message(COMPOSITOR_LOG_INFO, "Destroying compositor...");
    
    // 清理Vulkan资源
    if (g_compositor_state.config.use_hardware_acceleration) {
        compositor_vulkan_cleanup();
    }
    
    // 清理窗口
    cleanup_windows(&g_compositor_state);
    
    // 清理脏区域数组
    compositor_clear_dirty_rects();
    if (g_compositor_state.dirty_rects) {
        track_memory_free(sizeof(DirtyRect) * g_compositor_state.config.max_dirty_rects);
        free(g_compositor_state.dirty_rects);
        g_compositor_state.dirty_rects = NULL;
    }
    
    // 清理配置
    compositor_free_config(&g_compositor_state.config);
    
    // 清理各模块
    compositor_perf_cleanup();
    compositor_events_cleanup();
    compositor_window_cleanup();
    compositor_input_cleanup();
    compositor_utils_cleanup();
    
    g_initialized = false;
    log_message(COMPOSITOR_LOG_INFO, "Compositor destroyed successfully");
}

// 窗口清理函数现在在compositor_window.c中实现

// 设置窗口大小
int compositor_resize(int width, int height) {
    if (!g_initialized) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (width <= 0 || height <= 0) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid window size");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Resizing compositor to %dx%d", width, height);
    
    g_compositor_state.width = width;
    g_compositor_state.height = height;
    
    // 重建交换链
    if (g_compositor_state.config.use_hardware_acceleration) {
        if (recreate_swapchain(width, height) != COMPOSITOR_OK) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to resize compositor");
            return COMPOSITOR_ERROR_SWAPCHAIN_ERROR;
        }
    }
    
    // 标记需要重绘
    g_compositor_state.needs_redraw = true;
    
    return COMPOSITOR_OK;
}

// 获取活动窗口标题
const char* compositor_get_active_window_title() {
    if (!g_initialized) {
        return NULL;
    }
    
    if (g_compositor_state.active_window) {
        if (g_compositor_state.active_window_is_wayland) {
            WaylandWindow* window = (WaylandWindow*)g_compositor_state.active_window;
            return window->title;
        } else {
            XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state.active_window;
            return window->title;
        }
    }
    
    return NULL;
}

// 触发重绘 - 现在在compositor_render.c中实现

// 标记脏区域 - 现在在compositor_dirty.c中实现

// 清除脏区域 - 现在在compositor_dirty.c中实现

// 窗口排序函数现在在compositor_window.c中实现

// 获取窗口Z顺序
int compositor_get_window_z_order(const char* window_title) {
    if (!g_initialized || !window_title) {
        return -1;
    }
    
    // 查找Xwayland窗口
    for (int i = 0; i < g_compositor_state.xwayland_state.window_count; i++) {
        XwaylandWindowState* window = g_compositor_state.xwayland_state.windows[i];
        if (window->title && strcmp(window->title, window_title) == 0) {
            return window->z_order;
        }
    }
    
    // 查找Wayland窗口
    for (int i = 0; i < g_compositor_state.wayland_state.window_count; i++) {
        WaylandWindow* window = g_compositor_state.wayland_state.windows[i];
        if (window->title && strcmp(window->title, window_title) == 0) {
            return window->z_order;
        }
    }
    
    return -1;
}

// 设置窗口Z顺序
int compositor_set_window_z_order(const char* window_title, int z_order) {
    if (!g_initialized || !window_title) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    bool window_found = false;
    
    // 查找并设置Xwayland窗口Z顺序
    for (int i = 0; i < g_compositor_state.xwayland_state.window_count; i++) {
        XwaylandWindowState* window = g_compositor_state.xwayland_state.windows[i];
        if (window->title && strcmp(window->title, window_title) == 0) {
            window->z_order = z_order;
            window_found = true;
            break;
        }
    }
    
    // 查找并设置Wayland窗口Z顺序
    if (!window_found) {
        for (int i = 0; i < g_compositor_state.wayland_state.window_count; i++) {
            WaylandWindow* window = g_compositor_state.wayland_state.windows[i];
            if (window->title && strcmp(window->title, window_title) == 0) {
                window->z_order = z_order;
                window_found = true;
                break;
            }
        }
    }
    
    if (!window_found) {
        return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
    }
    
    // 重新排序窗口
    compositor_sort_windows_by_z_order();
    
    // 更新下一个Z顺序值
    if (z_order >= g_compositor_state.next_z_order) {
        g_compositor_state.next_z_order = z_order + 1;
    }
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    return COMPOSITOR_OK;
}

// 查找位置处的表面
void* find_surface_at_position(int x, int y, bool* out_is_wayland) {
    if (!g_initialized || !out_is_wayland) {
        return NULL;
    }
    
    // 从顶层开始查找（Z值较大的窗口在上方）
    // 先查找Wayland窗口（反向遍历，从顶层到底层）
    WaylandState* wayland_state = &g_compositor_state.wayland_state;
    for (int i = wayland_state->window_count - 1; i >= 0; i--) {
        WaylandWindow* window = wayland_state->windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x <= window->x + window->width + g_compositor_state.config.window_border_width * 2 &&
            y >= window->y && y <= window->y + window->height + g_compositor_state.config.window_border_width * 2 + g_compositor_state.config.window_titlebar_height) {
            *out_is_wayland = true;
            return window;
        }
    }
    
    // 再查找Xwayland窗口（反向遍历，从顶层到底层）
    XwaylandState* xwayland_state = &g_compositor_state.xwayland_state;
    for (int i = xwayland_state->window_count - 1; i >= 0; i--) {
        XwaylandWindowState* window = xwayland_state->windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x <= window->x + window->width + g_compositor_state.config.window_border_width * 2 &&
            y >= window->y && y <= window->y + window->height + g_compositor_state.config.window_border_width * 2 + g_compositor_state.config.window_titlebar_height) {
            *out_is_wayland = false;
            return window;
        }
    }
    
    *out_is_wayland = false;
    return NULL;
}

// 获取当前状态（仅供内部使用）
CompositorState* compositor_get_state(void) {
    return g_initialized ? &g_compositor_state : NULL;
}

// 检查是否已初始化
bool compositor_is_initialized(void) {
    return g_initialized;
}

// 工作区管理相关函数现在在 compositor_workspace..c 中实现

// 将窗口移动到指定工作区
// find_window_by_title 辅助函数现在在 compositor_workspace..c 中实现
// compositor_move_window_to_workspace 函数现在在 compositor_workspace..c 中实现

// collect_visible_windows 辅助函数现在在 compositor_workspace..c 中实现
// compositor_tile_windows 函数现在在 compositor_workspace..c 中实现

// compositor_cascade_windows 函数现在在 compositor_workspace..c 中实现

// compositor_group_windows 和 compositor_ungroup_windows 函数现在在 compositor_workspace..c 中实现

// compositor_close_all_windows 函数现在在 compositor_workspace..c 中实现    log_message(COMPOSITOR_LOG_INFO, "Closed %d windows", closed_count);
    
    return COMPOSITOR_OK;
}

// 最小化所有窗口
int compositor_minimize_all_windows() {
    if (!g_initialized) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 最小化当前工作区的所有窗口
    int minimized_count = 0;
    int i;
    
    // 最小化Xwayland窗口
    for (i = 0; i < g_compositor_state.xwayland_state.window_count; i++) {
        XwaylandWindowState* window = g_compositor_state.xwayland_state.windows[i];
        if (window->workspace_id == g_compositor_state.active_workspace || window->is_sticky) {
            window->is_minimized = true;
            window->state = WINDOW_STATE_MINIMIZED;
            minimized_count++;
        }
    }
    
    // 最小化Wayland窗口
    for (i = 0; i < g_compositor_state.wayland_state.window_count; i++) {
        WaylandWindow* window = g_compositor_state.wayland_state.windows[i];
        if (window->workspace_id == g_compositor_state.active_workspace || window->is_sticky) {
            window->is_minimized = true;
            window->state = WINDOW_STATE_MINIMIZED;
            minimized_count++;
        }
    }
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    log_message(COMPOSITOR_LOG_INFO, "Minimized %d windows", minimized_count);
    
    return COMPOSITOR_OK;
}

// 恢复所有窗口
int compositor_restore_all_windows() {
    if (!g_initialized) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 恢复当前工作区的所有窗口
    int restored_count = 0;
    int i;
    
    // 恢复Xwayland窗口
    for (i = 0; i < g_compositor_state.xwayland_state.window_count; i++) {
        XwaylandWindowState* window = g_compositor_state.xwayland_state.windows[i];
        if (window->workspace_id == g_compositor_state.active_workspace || window->is_sticky) {
            window->is_minimized = false;
            window->state = WINDOW_STATE_NORMAL;
            restored_count++;
        }
    }
    
    // 恢复Wayland窗口
    for (i = 0; i < g_compositor_state.wayland_state.window_count; i++) {
        WaylandWindow* window = g_compositor_state.wayland_state.windows[i];
        if (window->workspace_id == g_compositor_state.active_workspace || window->is_sticky) {
            window->is_minimized = false;
            window->state = WINDOW_STATE_NORMAL;
            restored_count++;
        }
    }
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    log_message(COMPOSITOR_LOG_INFO, "Restored %d windows", restored_count);
    
    return COMPOSITOR_OK;
}
