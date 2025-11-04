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

// 全局状态
static CompositorState g_compositor_state;
static bool g_initialized = false;

// 更新性能统计
static void update_performance_stats(void) {
    if (!g_initialized) return;
    
    int64_t current_time = get_current_time_ms();
    if (g_compositor_state.last_frame_time > 0) {
        float delta_time = (current_time - g_compositor_state.last_frame_time) / 1000.0f;
        if (delta_time > 0) {
            // 平滑FPS计算
            g_compositor_state.fps = 0.9f * g_compositor_state.fps + 0.1f * (1.0f / delta_time);
        }
    }
    g_compositor_state.last_frame_time = current_time;
    g_compositor_state.frame_count++;
    
    // 性能监控和调试输出
    if (g_compositor_state.config.performance_monitoring && g_compositor_state.frame_count % 60 == 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "FPS: %.1f, Avg frame time: %.2f ms", 
                   g_compositor_state.fps, g_compositor_state.avg_frame_time);
    }
    
    // 内存使用监控
    if (g_compositor_state.config.debug_mode && g_compositor_state.frame_count % 1000 == 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "Memory usage: %zu bytes (peak: %zu bytes)", 
                   g_compositor_state.total_allocated, g_compositor_state.peak_allocated);
    }
}

// 内存跟踪助手函数
static void track_memory_allocation(size_t size) {
    if (g_initialized && g_compositor_state.config.enable_memory_tracking) {
        g_compositor_state.total_allocated += size;
        if (g_compositor_state.total_allocated > g_compositor_state.peak_allocated) {
            g_compositor_state.peak_allocated = g_compositor_state.total_allocated;
        }
        
        // 检查内存使用限制
        size_t max_memory_bytes = g_compositor_state.config.max_memory_usage_mb * 1024 * 1024;
        if (max_memory_bytes > 0 && g_compositor_state.total_allocated > max_memory_bytes) {
            log_message(COMPOSITOR_LOG_WARN, "Memory usage exceeded limit: %zu / %zu bytes", 
                       g_compositor_state.total_allocated, max_memory_bytes);
        }
    }
}

static void track_memory_free(size_t size) {
    if (g_initialized && g_compositor_state.config.enable_memory_tracking) {
        if (g_compositor_state.total_allocated >= size) {
            g_compositor_state.total_allocated -= size;
        }
    }
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
    compositor_vulkan_set_state(&g_compositor_state);
    
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

// 动态扩容窗口数组
static int resize_window_array(void*** array, int32_t* capacity, int32_t new_capacity, size_t element_size) {
    void** new_array = (void**)safe_realloc(*array, element_size * new_capacity);
    if (!new_array) {
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    track_memory_free(element_size * *capacity);
    *array = new_array;
    *capacity = new_capacity;
    track_memory_allocation(element_size * new_capacity);
    return COMPOSITOR_OK;
}

// 添加Xwayland窗口（内部函数）
int add_xwayland_window(XwaylandWindowState* window) {
    if (!g_initialized || !window) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    XwaylandState* state = &g_compositor_state.xwayland_state;
    
    // 检查是否需要扩容
    if (state->window_count >= state->capacity) {
        int new_capacity = state->capacity * 2;
        if (new_capacity == 0) new_capacity = 8;
        
        // 检查是否超过最大窗口限制
        if (new_capacity > state->max_windows) {
            log_message(COMPOSITOR_LOG_ERROR, "Maximum window count reached");
            return COMPOSITOR_ERROR_MAX_WINDOWS;
        }
        
        if (resize_window_array((void***)&state->windows, &state->capacity, new_capacity, 
                               sizeof(XwaylandWindowState*)) != COMPOSITOR_OK) {
            return COMPOSITOR_ERROR_MEMORY;
        }
    }
    
    // 设置Z轴顺序
    window->z_order = g_compositor_state.next_z_order++;
    window->is_dirty = true;
    
    // 添加窗口
    state->windows[state->window_count++] = window;
    
    // 按Z轴顺序排序
    compositor_sort_windows_by_z_order();
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    log_message(COMPOSITOR_LOG_DEBUG, "Added Xwayland window: %s, Z-order: %d", 
               window->title ? window->title : "(null)", window->z_order);
    
    return COMPOSITOR_OK;
}

// 添加Wayland窗口（内部函数）
int add_wayland_window(WaylandWindow* window) {
    if (!g_initialized || !window) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    WaylandState* state = &g_compositor_state.wayland_state;
    
    // 检查是否需要扩容
    if (state->window_count >= state->capacity) {
        int new_capacity = state->capacity * 2;
        if (new_capacity == 0) new_capacity = 8;
        
        // 检查是否超过最大窗口限制
        if (new_capacity > state->max_windows) {
            log_message(COMPOSITOR_LOG_ERROR, "Maximum window count reached");
            return COMPOSITOR_ERROR_MAX_WINDOWS;
        }
        
        if (resize_window_array((void***)&state->windows, &state->capacity, new_capacity, 
                               sizeof(WaylandWindow*)) != COMPOSITOR_OK) {
            return COMPOSITOR_ERROR_MEMORY;
        }
    }
    
    // 设置Z轴顺序
    window->z_order = g_compositor_state.next_z_order++;
    window->is_dirty = true;
    
    // 添加窗口
    state->windows[state->window_count++] = window;
    
    // 按Z轴顺序排序
    compositor_sort_windows_by_z_order();
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    log_message(COMPOSITOR_LOG_DEBUG, "Added Wayland window: %s, Z-order: %d", 
               window->title ? window->title : "(null)", window->z_order);
    
    return COMPOSITOR_OK;
}

// 主循环单步
int compositor_step(void) {
    if (!g_initialized) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 获取当前时间
    int64_t current_time = get_current_time_ms();
    
    // 更新性能统计
    update_performance_stats();
    
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
            render_result = render_frame();
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
    
    // 更新最后帧时间
    g_compositor_state.last_frame_time = current_time;
    
    return COMPOSITOR_OK;
}

// 处理输入事件
void process_input_events() {
    if (!g_initialized) return;
    
    CompositorInputEvent event;
    while (compositor_input_get_next_event(&event)) {
        // 处理不同类型的输入事件
        switch (event.type) {
            case COMPOSITOR_INPUT_EVENT_MOUSE_MOTION:
                process_mouse_motion_event(&event);
                break;
                
            case COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON:
                process_mouse_button_event(&event);
                break;
                
            case COMPOSITOR_INPUT_EVENT_TOUCH:
                process_touch_event(&event);
                break;
                
            case COMPOSITOR_INPUT_EVENT_GESTURE:
                process_gesture_event(&event);
                break;
                
            default:
                if (g_compositor_state.config.debug_mode) {
                    log_message(COMPOSITOR_LOG_DEBUG, "Unhandled input event type: %d", event.type);
                }
                break;
        }
    }
}

// 处理鼠标移动事件
void process_mouse_motion_event(CompositorInputEvent* event) {
    // 如果正在拖动窗口，更新窗口位置
    if (g_compositor_state.is_dragging && g_compositor_state.dragging_window) {
        int new_x = event->mouse.x - g_compositor_state.drag_offset_x;
        int new_y = event->mouse.y - g_compositor_state.drag_offset_y;
        
        // 限制窗口不超出屏幕边界（可配置是否启用）
        if (g_compositor_state.config.restrict_window_bounds) {
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x + g_compositor_state.dragging_window->width > g_compositor_state.width) {
                new_x = g_compositor_state.width - g_compositor_state.dragging_window->width;
            }
            if (new_y + g_compositor_state.dragging_window->height > g_compositor_state.height) {
                new_y = g_compositor_state.height - g_compositor_state.dragging_window->height;
            }
        }
        
        // 更新窗口位置
        g_compositor_state.dragging_window->x = new_x;
        g_compositor_state.dragging_window->y = new_y;
        
        // 标记窗口区域为脏
        compositor_mark_dirty_rect(new_x, new_y, 
                                 g_compositor_state.dragging_window->width,
                                 g_compositor_state.dragging_window->height);
        
        // 触发重绘
        g_compositor_state.needs_redraw = true;
    }
}

// 处理鼠标按钮事件
void process_mouse_button_event(CompositorInputEvent* event) {
    // 只有左键点击才处理
    if (event->mouse_button.button != COMPOSITOR_MOUSE_BUTTON_LEFT) {
        return;
    }
    
    if (event->mouse_button.pressed) {
        // 查找点击位置的窗口
        bool is_wayland = false;
        void* window = find_surface_at_position(event->mouse_button.x, 
                                              event->mouse_button.y, &is_wayland);
        
        if (window) {
            // 记录拖动信息
            g_compositor_state.dragging_window = window;
            g_compositor_state.is_dragging = true;
            g_compositor_state.drag_offset_x = event->mouse_button.x - ((WaylandWindow*)window)->x;
            g_compositor_state.drag_offset_y = event->mouse_button.y - ((WaylandWindow*)window)->y;
            
            // 更新活动窗口
            g_compositor_state.active_window = window;
            g_compositor_state.active_window_is_wayland = is_wayland;
        } else {
            // 点击桌面背景，取消拖动
            g_compositor_state.is_dragging = false;
            g_compositor_state.dragging_window = NULL;
            g_compositor_state.active_window = NULL;
        }
    } else {
        // 释放鼠标按钮，结束拖动
        g_compositor_state.is_dragging = false;
    }
}

// 处理触摸事件
void process_touch_event(CompositorInputEvent* event) {
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
void process_gesture_event(CompositorInputEvent* event) {
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

// 合并脏区域（减少渲染区域）
void merge_dirty_rects(CompositorState* state) {
    if (!state || !state->dirty_rects || state->dirty_rect_count <= 1) return;
    
    // 简单的脏区域合并算法
    // 实际实现中可以使用更复杂的合并策略
    int merged_count = 0;
    for (int i = 0; i < state->dirty_rect_count; i++) {
        bool merged = false;
        for (int j = i + 1; j < state->dirty_rect_count; j++) {
            // 检查两个脏区域是否重叠或相邻
            if (state->dirty_rects[i].x < state->dirty_rects[j].x + state->dirty_rects[j].width &&
                state->dirty_rects[i].x + state->dirty_rects[i].width > state->dirty_rects[j].x &&
                state->dirty_rects[i].y < state->dirty_rects[j].y + state->dirty_rects[j].height &&
                state->dirty_rects[i].y + state->dirty_rects[i].height > state->dirty_rects[j].y) {
                // 合并区域
                int min_x = MIN(state->dirty_rects[i].x, state->dirty_rects[j].x);
                int min_y = MIN(state->dirty_rects[i].y, state->dirty_rects[j].y);
                int max_x = MAX(state->dirty_rects[i].x + state->dirty_rects[i].width, 
                               state->dirty_rects[j].x + state->dirty_rects[j].width);
                int max_y = MAX(state->dirty_rects[i].y + state->dirty_rects[i].height, 
                               state->dirty_rects[j].y + state->dirty_rects[j].height);
                
                state->dirty_rects[i].x = min_x;
                state->dirty_rects[i].y = min_y;
                state->dirty_rects[i].width = max_x - min_x;
                state->dirty_rects[i].height = max_y - min_y;
                
                // 移除已合并的区域
                if (j < state->dirty_rect_count - 1) {
                    state->dirty_rects[j] = state->dirty_rects[state->dirty_rect_count - 1];
                }
                state->dirty_rect_count--;
                j--; // 重新检查当前位置
                merged = true;
            }
        }
        if (!merged) {
            merged_count++;
        }
    }
    
    if (merged_count > 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "Merged dirty rects: %d -> %d", 
                   merged_count, state->dirty_rect_count);
    }
}

// 销毁合成器
void compositor_destroy(void) {
    if (!g_initialized) return;
    
    log_message(COMPOSITOR_LOG_INFO, "Destroying compositor...");
    
    // 清理Vulkan资源
    if (g_compositor_state.config.use_hardware_acceleration) {
        cleanup_vulkan();
    }
    
    // 清理窗口
    cleanup_windows(&g_compositor_state);
    
    // 清理脏区域数组
    if (g_compositor_state.dirty_rects) {
        track_memory_free(sizeof(DirtyRect) * g_compositor_state.config.max_dirty_rects);
        free(g_compositor_state.dirty_rects);
        g_compositor_state.dirty_rects = NULL;
    }
    
    // 清理配置
    compositor_free_config(&g_compositor_state.config);
    
    // 清理各模块
    compositor_window_cleanup();
    compositor_input_cleanup();
    compositor_utils_cleanup();
    
    g_initialized = false;
    log_message(COMPOSITOR_LOG_INFO, "Compositor destroyed successfully");
}

// 清理窗口资源
void cleanup_windows(CompositorState* state) {
    if (!state) return;
    
    // 清理Xwayland窗口
    if (state->xwayland_state.windows) {
        for (int i = 0; i < state->xwayland_state.window_count; i++) {
            if (state->xwayland_state.windows[i]) {
                if (state->xwayland_state.windows[i]->title) {
                    track_memory_free(strlen(state->xwayland_state.windows[i]->title) + 1);
                    free((void*)state->xwayland_state.windows[i]->title);
                }
                // 清理脏区域
                if (state->xwayland_state.windows[i]->dirty_regions) {
                    track_memory_free(sizeof(DirtyRect) * state->xwayland_state.windows[i]->dirty_region_count);
                    free(state->xwayland_state.windows[i]->dirty_regions);
                }
                // 清理渲染数据
                if (state->xwayland_state.windows[i]->render_data) {
                    free(state->xwayland_state.windows[i]->render_data);
                }
                track_memory_free(sizeof(XwaylandWindowState));
                free(state->xwayland_state.windows[i]);
            }
        }
        track_memory_free(sizeof(XwaylandWindowState*) * state->xwayland_state.capacity);
        free(state->xwayland_state.windows);
        state->xwayland_state.windows = NULL;
    }
    
    // 清理Wayland窗口
    if (state->wayland_state.windows) {
        for (int i = 0; i < state->wayland_state.window_count; i++) {
            if (state->wayland_state.windows[i]) {
                if (state->wayland_state.windows[i]->title) {
                    track_memory_free(strlen(state->wayland_state.windows[i]->title) + 1);
                    free((void*)state->wayland_state.windows[i]->title);
                }
                // 清理脏区域
                if (state->wayland_state.windows[i]->dirty_regions) {
                    track_memory_free(sizeof(DirtyRect) * state->wayland_state.windows[i]->dirty_region_count);
                    free(state->wayland_state.windows[i]->dirty_regions);
                }
                // 清理渲染数据
                if (state->wayland_state.windows[i]->render_data) {
                    free(state->wayland_state.windows[i]->render_data);
                }
                track_memory_free(sizeof(WaylandWindow));
                free(state->wayland_state.windows[i]);
            }
        }
        track_memory_free(sizeof(WaylandWindow*) * state->wayland_state.capacity);
        free(state->wayland_state.windows);
        state->wayland_state.windows = NULL;
    }
    
    // 重置窗口计数
    state->xwayland_state.window_count = 0;
    state->wayland_state.window_count = 0;
}

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

// 触发重绘
void compositor_schedule_redraw(void) {
    if (g_initialized) {
        g_compositor_state.needs_redraw = true;
    }
}

// 标记脏区域
void compositor_mark_dirty_rect(int x, int y, int width, int height) {
    if (!g_initialized || !g_compositor_state.use_dirty_rect_optimization || 
        !g_compositor_state.dirty_rects || width <= 0 || height <= 0) {
        return;
    }
    
    // 限制脏区域在屏幕范围内
    x = MAX(x, 0);
    y = MAX(y, 0);
    width = MIN(width, g_compositor_state.width - x);
    height = MIN(height, g_compositor_state.height - y);
    
    // 检查是否达到最大脏区域数量
    if (g_compositor_state.dirty_rect_count >= g_compositor_state.config.max_dirty_rects) {
        // 如果达到上限，重置为全屏重绘
        g_compositor_state.dirty_rect_count = 1;
        g_compositor_state.dirty_rects[0].x = 0;
        g_compositor_state.dirty_rects[0].y = 0;
        g_compositor_state.dirty_rects[0].width = g_compositor_state.width;
        g_compositor_state.dirty_rects[0].height = g_compositor_state.height;
        return;
    }
    
    // 添加脏区域
    g_compositor_state.dirty_rects[g_compositor_state.dirty_rect_count].x = x;
    g_compositor_state.dirty_rects[g_compositor_state.dirty_rect_count].y = y;
    g_compositor_state.dirty_rects[g_compositor_state.dirty_rect_count].width = width;
    g_compositor_state.dirty_rects[g_compositor_state.dirty_rect_count].height = height;
    g_compositor_state.dirty_rect_count++;
    
    // 合并脏区域
    if (g_compositor_state.dirty_rect_count >= 4) { // 当脏区域数量较多时进行合并
        merge_dirty_rects(&g_compositor_state);
    }
}

// 清除脏区域
void compositor_clear_dirty_rects(void) {
    if (!g_initialized) return;
    
    g_compositor_state.dirty_rect_count = 0;
}

// 按Z顺序排序窗口（Xwayland窗口排序函数）
static int compare_xwayland_windows(const void* a, const void* b) {
    XwaylandWindowState* window_a = *((XwaylandWindowState**)a);
    XwaylandWindowState* window_b = *((XwaylandWindowState**)b);
    return window_a->z_order - window_b->z_order;
}

// 按Z顺序排序窗口（Wayland窗口排序函数）
static int compare_wayland_windows(const void* a, const void* b) {
    WaylandWindow* window_a = *((WaylandWindow**)a);
    WaylandWindow* window_b = *((WaylandWindow**)b);
    return window_a->z_order - window_b->z_order;
}

// 按Z顺序排序窗口
void compositor_sort_windows_by_z_order(void) {
    if (!g_initialized) return;
    
    // 排序Xwayland窗口
    if (g_compositor_state.xwayland_state.windows && 
        g_compositor_state.xwayland_state.window_count > 1) {
        qsort(g_compositor_state.xwayland_state.windows, 
              g_compositor_state.xwayland_state.window_count, 
              sizeof(XwaylandWindowState*), 
              compare_xwayland_windows);
    }
    
    // 排序Wayland窗口
    if (g_compositor_state.wayland_state.windows && 
        g_compositor_state.wayland_state.window_count > 1) {
        qsort(g_compositor_state.wayland_state.windows, 
              g_compositor_state.wayland_state.window_count, 
              sizeof(WaylandWindow*), 
              compare_wayland_windows);
    }
}

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
