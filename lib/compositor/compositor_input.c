#include "compositor_input.h"
#include "compositor_window.h"
#include "compositor_utils.h"
#include <string.h>
#include <math.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 输入设备列表
static CompositorInputDevice* g_input_devices = NULL;
static int g_input_device_count = 0;
static int g_input_device_capacity = 0;

// 输入捕获模式
static CompositorInputCaptureMode g_input_capture_mode = COMPOSITOR_INPUT_CAPTURE_MODE_NORMAL;

// 活动输入设备
static CompositorInputDevice* g_active_device = NULL;

// Alt+Tab功能相关静态变量
static bool g_alt_key_pressed = false;
static bool g_window_switching = false;
static int g_selected_window_index = 0;
static void** g_window_list = NULL;
static int g_window_list_count = 0;
static bool* g_window_is_wayland_list = NULL;

// 手势状态
static struct {
    bool is_active;
    CompositorGestureType type;
    int start_x[5];  // 支持多点触控
    int start_y[5];
    int current_x[5];
    int current_y[5];
    int touch_count;
    int64_t start_time;
    float scale;     // 缩放因子
    float rotation;  // 旋转角度
} g_gesture_state = {0};

// 查找指定位置的表面
static void* find_surface_at_position(int x, int y, bool* is_wayland) {
    if (!g_compositor_state || !is_wayland) {
        return NULL;
    }
    
    // 先检查Wayland窗口（从顶层开始）
    for (int i = g_compositor_state->wayland_state.window_count - 1; i >= 0; i--) {
        WaylandWindow* window = g_compositor_state->wayland_state.windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            *is_wayland = true;
            return window;
        }
    }
    
    // 再检查Xwayland窗口
    for (int i = g_compositor_state->xwayland_state.window_count - 1; i >= 0; i--) {
        XwaylandWindowState* window = g_compositor_state->xwayland_state.windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            *is_wayland = false;
            return window;
        }
    }
    
    return NULL;
}

// 安全内存分配
static void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory of size %zu", size);
    }
    return ptr;
}

// 安全内存重分配
static void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to reallocate memory of size %zu", size);
    }
    return new_ptr;
}

// 获取当前时间（毫秒）
static int64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 初始化输入设备
static CompositorInputDevice* create_input_device(CompositorInputDeviceType type, const char* name, int device_id) {
    CompositorInputDevice* device = (CompositorInputDevice*)safe_malloc(sizeof(CompositorInputDevice));
    if (!device) {
        return NULL;
    }
    
    memset(device, 0, sizeof(CompositorInputDevice));
    device->type = type;
    device->device_id = device_id;
    device->name = strdup(name ? name : "Unknown Device");
    device->enabled = true;
    
    return device;
}

// 注册输入设备
int compositor_input_register_device(CompositorInputDeviceType type, const char* name, int device_id) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 检查设备是否已注册
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            log_message(COMPOSITOR_LOG_WARN, "Device already registered: %d", device_id);
            return COMPOSITOR_ERROR_DEVICE_EXISTS;
        }
    }
    
    // 检查是否需要扩容
    if (g_input_device_count >= g_input_device_capacity) {
        int new_capacity = g_input_device_capacity == 0 ? 8 : g_input_device_capacity * 2;
        CompositorInputDevice* new_devices = (CompositorInputDevice*)safe_realloc(
            g_input_devices, sizeof(CompositorInputDevice) * new_capacity);
        if (!new_devices) {
            return COMPOSITOR_ERROR_MEMORY;
        }
        g_input_devices = new_devices;
        g_input_device_capacity = new_capacity;
    }
    
    // 创建并添加设备
    CompositorInputDevice* device = create_input_device(type, name, device_id);
    if (!device) {
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    g_input_devices[g_input_device_count++] = *device;
    free(device);  // 数据已经复制到数组中
    
    log_message(COMPOSITOR_LOG_INFO, "Registered input device: %s (ID: %d, Type: %d)", 
               name, device_id, type);
    
    return COMPOSITOR_OK;
}

// 注销输入设备
int compositor_input_unregister_device(int device_id) {
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            // 释放设备名称
            if (g_input_devices[i].name) {
                free((void*)g_input_devices[i].name);
            }
            
            // 移除设备（将最后一个设备移到当前位置）
            if (i < g_input_device_count - 1) {
                g_input_devices[i] = g_input_devices[g_input_device_count - 1];
            }
            g_input_device_count--;
            
            log_message(COMPOSITOR_LOG_INFO, "Unregistered input device: ID %d", device_id);
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
}

// 获取输入设备
CompositorInputDevice* compositor_input_get_device(int device_id) {
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            return &g_input_devices[i];
        }
    }
    return NULL;
}

// 设置设备启用状态
int compositor_input_set_device_enabled(int device_id, bool enabled) {
    CompositorInputDevice* device = compositor_input_get_device(device_id);
    if (!device) {
        return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
    }
    
    device->enabled = enabled;
    log_message(COMPOSITOR_LOG_DEBUG, "Device %d enabled: %s", device_id, enabled ? "true" : "false");
    
    return COMPOSITOR_OK;
}

// 设置输入捕获模式
void compositor_input_set_capture_mode(CompositorInputCaptureMode mode) {
    g_input_capture_mode = mode;
    log_message(COMPOSITOR_LOG_DEBUG, "Input capture mode set to: %d", mode);
}

// 获取输入捕获模式
CompositorInputCaptureMode compositor_input_get_capture_mode(void) {
    return g_input_capture_mode;
}

// 清理窗口列表资源
static void cleanup_window_list(void) {
    if (g_window_list != NULL) {
        free(g_window_list);
        g_window_list = NULL;
    }
    if (g_window_is_wayland_list != NULL) {
        free(g_window_is_wayland_list);
        g_window_is_wayland_list = NULL;
    }
    g_window_list_count = 0;
    g_selected_window_index = 0;
}

// 收集所有可见窗口
static void collect_visible_windows(void) {
    cleanup_window_list();
    
    // 统计窗口数量
    int count = 0;
    
    // 检查Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            xwayland_state->windows[i]->surface != NULL) {
            count++;
        }
    }
    
    // 检查Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            wayland_state->windows[i]->surface != NULL) {
            count++;
        }
    }
    
    // 分配内存
    if (count > 0) {
        g_window_list = (void**)malloc(count * sizeof(void*));
        g_window_is_wayland_list = (bool*)malloc(count * sizeof(bool));
        g_window_list_count = 0;
        
        // 填充Xwayland窗口
        for (int i = 0; i < xwayland_state->window_count; i++) {
            if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                xwayland_state->windows[i]->surface != NULL) {
                g_window_list[g_window_list_count] = xwayland_state->windows[i];
                g_window_is_wayland_list[g_window_list_count] = false;
                g_window_list_count++;
            }
        }
        
        // 填充Wayland窗口
        for (int i = 0; i < wayland_state->window_count; i++) {
            if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                wayland_state->windows[i]->surface != NULL) {
                g_window_list[g_window_list_count] = wayland_state->windows[i];
                g_window_is_wayland_list[g_window_list_count] = true;
                g_window_list_count++;
            }
        }
    }
}

// 高亮显示选中的窗口
static void highlight_selected_window(void) {
    // 重置所有窗口透明度
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        xwayland_state->windows[i]->opacity = 1.0f;
    }
    
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        wayland_state->windows[i]->opacity = 1.0f;
    }
    
    // 降低非选中窗口的透明度
    if (g_window_list_count > 0 && g_selected_window_index >= 0 && g_selected_window_index < g_window_list_count) {
        for (int i = 0; i < g_window_list_count; i++) {
            if (i != g_selected_window_index) {
                if (g_window_is_wayland_list[i]) {
                    WaylandWindow* window = (WaylandWindow*)g_window_list[i];
                    window->opacity = 0.4f;
                } else {
                    XwaylandWindowState* window = (XwaylandWindowState*)g_window_list[i];
                    window->opacity = 0.4f;
                }
            }
        }
    }
}

// 激活选中的窗口
static void activate_selected_window(void) {
    if (g_window_list_count > 0 && g_selected_window_index >= 0 && g_selected_window_index < g_window_list_count) {
        if (g_window_is_wayland_list[g_selected_window_index]) {
            WaylandWindow* wayland_window = (WaylandWindow*)g_window_list[g_selected_window_index];
            
            // 激活Wayland窗口
            wayland_window_activate(wayland_window);
            
            // 更新全局活动窗口信息
            if (g_compositor_state) {
                g_compositor_state->active_window = wayland_window;
                g_compositor_state->active_window_is_wayland = true;
            }
        } else {
            XwaylandWindowState* xwayland_window = (XwaylandWindowState*)g_window_list[g_selected_window_index];
            
            // 激活Xwayland窗口
            xwayland_window_activate(xwayland_window);
            
            // 更新全局活动窗口信息
            if (g_compositor_state) {
                g_compositor_state->active_window = xwayland_window;
                g_compositor_state->active_window_is_wayland = false;
            }
        }
    }
}

// 处理手势开始
static void handle_gesture_start(CompositorInputEvent* event) {
    if (event->touch_count > 0) {
        memset(&g_gesture_state, 0, sizeof(g_gesture_state));
        g_gesture_state.is_active = true;
        g_gesture_state.touch_count = event->touch_count;
        g_gesture_state.start_time = get_current_time_ms();
        g_gesture_state.scale = 1.0f;
        g_gesture_state.rotation = 0.0f;
        
        // 保存触摸点初始位置
        for (int i = 0; i < event->touch_count && i < 5; i++) {
            g_gesture_state.start_x[i] = event->touches[i].x;
            g_gesture_state.start_y[i] = event->touches[i].y;
            g_gesture_state.current_x[i] = event->touches[i].x;
            g_gesture_state.current_y[i] = event->touches[i].y;
        }
        
        // 根据触摸点数确定手势类型
        if (event->touch_count == 1) {
            g_gesture_state.type = COMPOSITOR_GESTURE_TYPE_TAP;
        } else if (event->touch_count == 2) {
            g_gesture_state.type = COMPOSITOR_GESTURE_TYPE_PINCH;
        } else if (event->touch_count >= 3) {
            g_gesture_state.type = COMPOSITOR_GESTURE_TYPE_SWIPE;
        }
    }
}

// 处理手势更新
static void handle_gesture_update(CompositorInputEvent* event) {
    if (!g_gesture_state.is_active || event->touch_count != g_gesture_state.touch_count) {
        return;
    }
    
    // 更新触摸点当前位置
    for (int i = 0; i < event->touch_count && i < 5; i++) {
        g_gesture_state.current_x[i] = event->touches[i].x;
        g_gesture_state.current_y[i] = event->touches[i].y;
    }
    
    // 计算手势参数
    CompositorGestureInfo gesture_info;
    memset(&gesture_info, 0, sizeof(CompositorGestureInfo));
    gesture_info.type = g_gesture_state.type;
    gesture_info.touch_count = g_gesture_state.touch_count;
    
    // 根据手势类型计算不同参数
    if (gesture_info.type == COMPOSITOR_GESTURE_TYPE_PINCH && event->touch_count >= 2) {
        // 计算捏合缩放
        int start_dist = sqrt(
            pow(g_gesture_state.start_x[1] - g_gesture_state.start_x[0], 2) +
            pow(g_gesture_state.start_y[1] - g_gesture_state.start_y[0], 2)
        );
        int current_dist = sqrt(
            pow(g_gesture_state.current_x[1] - g_gesture_state.current_x[0], 2) +
            pow(g_gesture_state.current_y[1] - g_gesture_state.current_y[0], 2)
        );
        
        if (start_dist > 0) {
            g_gesture_state.scale = (float)current_dist / start_dist;
            gesture_info.scale = g_gesture_state.scale;
        }
        
        // 计算旋转角度
        float start_angle = atan2(
            g_gesture_state.start_y[1] - g_gesture_state.start_y[0],
            g_gesture_state.start_x[1] - g_gesture_state.start_x[0]
        ) * 180.0f / M_PI;
        float current_angle = atan2(
            g_gesture_state.current_y[1] - g_gesture_state.current_y[0],
            g_gesture_state.current_x[1] - g_gesture_state.current_x[0]
        ) * 180.0f / M_PI;
        
        g_gesture_state.rotation = current_angle - start_angle;
        gesture_info.rotation = g_gesture_state.rotation;
    } else if (gesture_info.type == COMPOSITOR_GESTURE_TYPE_SWIPE && event->touch_count >= 3) {
        // 计算滑动方向和距离
        int avg_dx = 0, avg_dy = 0;
        for (int i = 0; i < event->touch_count && i < 5; i++) {
            avg_dx += g_gesture_state.current_x[i] - g_gesture_state.start_x[i];
            avg_dy += g_gesture_state.current_y[i] - g_gesture_state.start_y[i];
        }
        avg_dx /= event->touch_count;
        avg_dy /= event->touch_count;
        
        gesture_info.delta_x = avg_dx;
        gesture_info.delta_y = avg_dy;
    }
    
    // 这里可以添加手势处理逻辑
    log_message(COMPOSITOR_LOG_DEBUG, "Gesture update: type=%d, scale=%.2f, rotation=%.2f, dx=%d, dy=%d", 
               gesture_info.type, gesture_info.scale, gesture_info.rotation, gesture_info.delta_x, gesture_info.delta_y);
    
    // 查找手势作用的窗口
    bool is_wayland = false;
    void* surface = find_surface_at_position(event->touches[0].x, event->touches[0].y, &is_wayland);
    
    // 处理窗口操作手势
    if (surface) {
        // 捏合缩放操作
        if (gesture_info.type == COMPOSITOR_GESTURE_TYPE_PINCH) {
            // 实现窗口缩放
            // 注意：实际应用中需要添加边界检查和最小/最大缩放比例
        }
        
        // 滑动操作移动窗口
        if (gesture_info.type == COMPOSITOR_GESTURE_TYPE_SWIPE) {
            int new_x, new_y;
            if (is_wayland) {
                WaylandWindow* window = (WaylandWindow*)surface;
                new_x = window->x + gesture_info.delta_x;
                new_y = window->y + gesture_info.delta_y;
                
                // 边界检查
                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                
                window->x = new_x;
                window->y = new_y;
            } else {
                XwaylandWindowState* window = (XwaylandWindowState*)surface;
                new_x = window->x + gesture_info.delta_x;
                new_y = window->y + gesture_info.delta_y;
                
                // 边界检查
                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                
                window->x = new_x;
                window->y = new_y;
            }
            
            // 标记脏区域
            compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
        }
    }
}

// 处理手势结束
static void handle_gesture_end(void) {
    if (!g_gesture_state.is_active) {
        return;
    }
    
    int64_t duration = get_current_time_ms() - g_gesture_state.start_time;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Gesture ended: type=%d, duration=%lldms", 
               g_gesture_state.type, duration);
    
    // 重置手势状态
    g_gesture_state.is_active = false;
}

// 处理输入事件（新接口）
int compositor_handle_input_event(CompositorInputEvent* event) {
    if (!g_compositor_state || !event) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 检查输入设备是否启用
    if (event->device_id != -1) {
        CompositorInputDevice* device = compositor_input_get_device(event->device_id);
        if (device && !device->enabled) {
            return COMPOSITOR_OK;  // 设备已禁用，忽略事件
        }
    }
    
    // 根据输入捕获模式过滤事件
    if (g_input_capture_mode == COMPOSITOR_INPUT_CAPTURE_MODE_DISABLED) {
        return COMPOSITOR_OK;  // 输入已禁用
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Handling input event: type=%d, device_id=%d", 
               event->type, event->device_id);
    
    // 处理不同类型的输入事件
    switch (event->type) {
        case COMPOSITOR_INPUT_EVENT_MOUSE_MOTION:
            // 鼠标移动事件
            log_message(COMPOSITOR_LOG_DEBUG, "Mouse motion: x=%d, y=%d", event->x, event->y);
            
            // 拖动窗口逻辑
            if (g_compositor_state->dragging && g_compositor_state->drag_window) {
                // 计算新位置
                int new_x = g_compositor_state->drag_start_x + (event->x - g_compositor_state->mouse_start_x);
                int new_y = g_compositor_state->drag_start_y + (event->y - g_compositor_state->mouse_start_y);
                
                // 边界检查
                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                if (new_x > g_compositor_state->width - g_compositor_state->drag_window_width - WINDOW_BORDER_WIDTH * 2) {
                    new_x = g_compositor_state->width - g_compositor_state->drag_window_width - WINDOW_BORDER_WIDTH * 2;
                }
                if (new_y > g_compositor_state->height - g_compositor_state->drag_window_height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT) {
                    new_y = g_compositor_state->height - g_compositor_state->drag_window_height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT;
                }
                
                // 更新窗口位置
                if (g_compositor_state->drag_is_wayland_window) {
                    WaylandWindow* window = (WaylandWindow*)g_compositor_state->drag_window;
                    window->x = new_x;
                    window->y = new_y;
                } else {
                    XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state->drag_window;
                    window->x = new_x;
                    window->y = new_y;
                }
                
                // 标记脏区域
                compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON:
            // 鼠标按钮事件
            log_message(COMPOSITOR_LOG_DEBUG, "Mouse button: button=%d, state=%d, x=%d, y=%d", 
                       event->button, event->state, event->x, event->y);
            
            if (event->state == COMPOSITOR_INPUT_STATE_PRESSED) {
                // 鼠标按下，查找点击的窗口
                bool is_wayland = false;
                void* surface = find_surface_at_position(event->x, event->y, &is_wayland);
                if (surface) {
                    // 激活点击的窗口
                    if (is_wayland) {
                        WaylandWindow* window = (WaylandWindow*)surface;
                        wayland_window_activate(window);
                        g_compositor_state->active_window = window;
                        g_compositor_state->active_window_is_wayland = true;
                    } else {
                        XwaylandWindowState* window = (XwaylandWindowState*)surface;
                        xwayland_window_activate(window);
                        g_compositor_state->active_window = window;
                        g_compositor_state->active_window_is_wayland = false;
                    }
                    
                    // 标记脏区域
                    compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
                }
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_KEYBOARD:
            // 键盘事件
            log_message(COMPOSITOR_LOG_DEBUG, "Keyboard: keycode=%d, state=%d, modifiers=%d", 
                       event->keycode, event->state, event->modifiers);
            
            // Alt键处理
            if (event->keycode == 56 || event->keycode == 184) { // Alt键 (左右)
                if (event->state == COMPOSITOR_INPUT_STATE_PRESSED) {
                    g_alt_key_pressed = true;
                } else if (event->state == COMPOSITOR_INPUT_STATE_RELEASED) {
                    g_alt_key_pressed = false;
                    
                    // 当Alt键释放时，如果正在窗口切换状态，激活选中的窗口
                    if (g_window_switching) {
                        activate_selected_window();
                        g_window_switching = false;
                        cleanup_window_list();
                        
                        // 重置所有窗口透明度
                        XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
                        for (int i = 0; i < xwayland_state->window_count; i++) {
                            xwayland_state->windows[i]->opacity = 1.0f;
                        }
                        
                        WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
                        for (int i = 0; i < wayland_state->window_count; i++) {
                            wayland_state->windows[i]->opacity = 1.0f;
                        }
                        
                        compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
                    }
                }
            }
            
            // Tab键处理 (Alt+Tab)
            if (event->keycode == 15 && event->state == COMPOSITOR_INPUT_STATE_PRESSED && g_alt_key_pressed) {
                // 开始窗口切换或切换到下一个窗口
                if (!g_window_switching) {
                    g_window_switching = true;
                    collect_visible_windows();
                    g_selected_window_index = 0;
                } else {
                    // 循环选择下一个窗口
                    g_selected_window_index = (g_selected_window_index + 1) % g_window_list_count;
                }
                
                // 高亮显示选中的窗口
                highlight_selected_window();
                compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            
            // Alt+F4 关闭窗口
            if (event->keycode == 62 && event->state == COMPOSITOR_INPUT_STATE_PRESSED && g_alt_key_pressed) {
                // 关闭当前活动窗口
                if (g_compositor_state->active_window) {
                    if (g_compositor_state->active_window_is_wayland) {
                        WaylandWindow* window = (WaylandWindow*)g_compositor_state->active_window;
                        wayland_window_close(window);
                    } else {
                        XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state->active_window;
                        xwayland_window_close(window);
                    }
                }
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_TOUCH:
            // 触摸事件
            log_message(COMPOSITOR_LOG_DEBUG, "Touch: count=%d, state=%d", 
                       event->touch_count, event->state);
            
            if (event->state == COMPOSITOR_INPUT_STATE_PRESSED) {
                handle_gesture_start(event);
            } else if (event->state == COMPOSITOR_INPUT_STATE_MOTION) {
                handle_gesture_update(event);
            } else if (event->state == COMPOSITOR_INPUT_STATE_RELEASED) {
                handle_gesture_end();
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_PEN:
            // 触控笔事件
            log_message(COMPOSITOR_LOG_DEBUG, "Pen: x=%d, y=%d, pressure=%.2f, tilt_x=%d, tilt_y=%d", 
                       event->x, event->y, event->pen_pressure, event->pen_tilt_x, event->pen_tilt_y);
            // 实现触控笔特殊功能（压力感应等）
            break;
            
        case COMPOSITOR_INPUT_EVENT_JOYSTICK:
            // 摇杆事件
            log_message(COMPOSITOR_LOG_DEBUG, "Joystick: axis_x=%.2f, axis_y=%.2f, button=%d", 
                       event->joystick_axis_x, event->joystick_axis_y, event->button);
            // 实现摇杆输入处理
            break;
            
        case COMPOSITOR_INPUT_EVENT_SCROLL:
            // 滚轮/滚动事件
            log_message(COMPOSITOR_LOG_DEBUG, "Scroll: dx=%d, dy=%d, direction=%d", 
                       event->scroll_dx, event->scroll_dy, event->scroll_direction);
            // 实现滚动事件处理
            break;
            
        case COMPOSITOR_INPUT_EVENT_GESTURE:
            // 手势事件
            log_message(COMPOSITOR_LOG_DEBUG, "Gesture: type=%d, scale=%.2f, rotation=%.2f, x=%d, y=%d", 
                       event->gesture_type, event->gesture_scale, event->gesture_rotation, event->x, event->y);
            
            // 查找手势作用的窗口
            bool is_wayland = false;
            void* surface = find_surface_at_position(event->x, event->y, &is_wayland);
            
            if (surface) {
                // 根据手势类型处理
                switch (event->gesture_type) {
                    case COMPOSITOR_GESTURE_TYPE_PINCH:
                        // 处理捏合缩放手势
                        log_message(COMPOSITOR_LOG_DEBUG, "Pinch gesture detected, scale: %.2f", event->gesture_scale);
                        // 实现窗口缩放
                        break;
                        
                    case COMPOSITOR_GESTURE_TYPE_ROTATE:
                        // 处理旋转手势
                        log_message(COMPOSITOR_LOG_DEBUG, "Rotate gesture detected, angle: %.2f", event->gesture_rotation);
                        // 实现窗口旋转
                        break;
                        
                    case COMPOSITOR_GESTURE_TYPE_SWIPE:
                        // 处理滑动手势
                        log_message(COMPOSITOR_LOG_DEBUG, "Swipe gesture detected, direction: %d", event->scroll_direction);
                        // 实现窗口切换或其他功能
                        if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_UP) {
                            // 向上滑动，可能实现窗口最小化或其他功能
                        } else if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_DOWN) {
                            // 向下滑动，可能实现显示任务栏或其他功能
                        }
                        break;
                        
                    case COMPOSITOR_GESTURE_TYPE_TAP:
                        // 处理点击手势
                        log_message(COMPOSITOR_LOG_DEBUG, "Tap gesture detected");
                        break;
                        
                    default:
                        log_message(COMPOSITOR_LOG_WARN, "Unknown gesture type: %d", event->gesture_type);
                        break;
                }
            }
            break;
            
        default:
            log_message(COMPOSITOR_LOG_WARN, "Unknown input event type: %d", event->type);
            break;
    }
    
    return COMPOSITOR_OK;
}

// 处理输入事件（兼容旧接口）
void compositor_handle_input(int type, int x, int y, int key, int state) {
    if (!g_compositor_state) {
        log_message(COMPOSITOR_LOG_ERROR, "Compositor not initialized, cannot handle input");
        return;
    }
    
    // 记录输入事件（调试用）
    if (g_compositor_state->config.debug_mode) {
        log_message(COMPOSITOR_LOG_DEBUG, "Input event (legacy): type=%d, x=%d, y=%d, key=%d, state=%d", 
                   type, x, y, key, state);
    }
    
    // 将旧的输入事件映射到新的事件结构
    CompositorInputEvent event;
    memset(&event, 0, sizeof(CompositorInputEvent));
    event.device_id = -1;  // 默认设备
    
    switch (type) {
        case COMPOSITOR_INPUT_MOTION:
            event.type = COMPOSITOR_INPUT_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            event.state = COMPOSITOR_INPUT_STATE_MOTION;
            break;
            
        case COMPOSITOR_INPUT_BUTTON:
            event.type = COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON;
            event.x = x;
            event.y = y;
            event.button = key;
            event.state = (state == COMPOSITOR_INPUT_STATE_DOWN) ? COMPOSITOR_INPUT_STATE_PRESSED : COMPOSITOR_INPUT_STATE_RELEASED;
            break;
            
        case COMPOSITOR_INPUT_KEY:
            event.type = COMPOSITOR_INPUT_EVENT_KEYBOARD;
            event.keycode = key;
            event.state = (state == COMPOSITOR_INPUT_STATE_DOWN) ? COMPOSITOR_INPUT_STATE_PRESSED : COMPOSITOR_INPUT_STATE_RELEASED;
            break;
            
        case COMPOSITOR_INPUT_TOUCH:
            event.type = COMPOSITOR_INPUT_EVENT_TOUCH;
            event.x = x;
            event.y = y;
            event.state = (state == COMPOSITOR_INPUT_STATE_DOWN) ? COMPOSITOR_INPUT_STATE_PRESSED : 
                          (state == COMPOSITOR_INPUT_STATE_UP) ? COMPOSITOR_INPUT_STATE_RELEASED : COMPOSITOR_INPUT_STATE_MOTION;
            event.touch_count = 1;
            event.touches[0].x = x;
            event.touches[0].y = y;
            break;
            
        default:
            log_message(COMPOSITOR_LOG_WARN, "Unknown legacy input event type: %d", type);
            return;
    }
    
    // 使用新的处理函数处理事件
    compositor_handle_input_event(&event);
}

// 设置输入状态指针
void compositor_input_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 清理输入资源
void compositor_input_cleanup(void) {
    // 释放输入设备资源
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].name) {
            free((void*)g_input_devices[i].name);
        }
    }
    
    if (g_input_devices) {
        free(g_input_devices);
        g_input_devices = NULL;
    }
    
    // 清理窗口列表资源
    cleanup_window_list();
    
    // 重置状态
    g_input_device_count = 0;
    g_input_device_capacity = 0;
    g_compositor_state = NULL;
    g_active_device = NULL;
    g_alt_key_pressed = false;
    g_window_switching = false;
    
    // 重置手势状态
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
}

// 获取输入设备列表
int compositor_input_get_devices(CompositorInputDevice** out_devices, int* out_count) {
    if (!out_devices || !out_count) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    *out_devices = g_input_devices;
    *out_count = g_input_device_count;
    
    return COMPOSITOR_OK;
}

// 设置活动输入设备
void compositor_input_set_active_device(int device_id) {
    g_active_device = compositor_input_get_device(device_id);
}

// 获取活动输入设备
CompositorInputDevice* compositor_input_get_active_device(void) {
    return g_active_device;
}

// 模拟输入事件
int compositor_input_simulate_event(CompositorInputEventType type, int x, int y, int state) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    CompositorInputEvent event;
    memset(&event, 0, sizeof(CompositorInputEvent));
    event.type = type;
    event.x = x;
    event.y = y;
    event.state = state;
    event.device_id = -1;  // 模拟事件
    
    return compositor_handle_input_event(&event);
}