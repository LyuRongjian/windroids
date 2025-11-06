/*
 * WinDroids Compositor
 * Input Event Handling Implementation
 */

#include "compositor_input_event.h"
#include "compositor_input.h"
#include "compositor_window.h"
#include "compositor_utils.h"
#include <string.h>
#include <math.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 输入捕获模式
static CompositorInputCaptureMode g_input_capture_mode = COMPOSITOR_INPUT_CAPTURE_MODE_NORMAL;

// 设置合成器状态引用
void compositor_input_event_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 初始化事件处理系统
int compositor_input_event_init(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 初始化输入捕获模式
    g_input_capture_mode = COMPOSITOR_INPUT_CAPTURE_MODE_NORMAL;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Input event handling system initialized");
    return COMPOSITOR_OK;
}

// 清理事件处理系统
void compositor_input_event_cleanup(void) {
    // 清理相关资源
    log_message(COMPOSITOR_LOG_DEBUG, "Input event handling system cleaned up");
}

// 处理窗口事件
void process_window_events(CompositorState* state) {
    if (!state) return;
    
    // 处理Xwayland窗口事件
    for (int i = 0; i < state->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = state->xwayland_state.windows[i];
        if (window && window->needs_update) {
            // 标记窗口为脏区域
            compositor_mark_dirty_rect(window->x, window->y, window->width, window->height);
            window->needs_update = false;
        }
    }
    
    // 处理Wayland窗口事件
    for (int i = 0; i < state->wayland_state.window_count; i++) {
        WaylandWindow* window = state->wayland_state.windows[i];
        if (window && window->needs_update) {
            // 标记窗口为脏区域
            compositor_mark_dirty_rect(window->x, window->y, window->width, window->height);
            window->needs_update = false;
        }
    }
}

// 查找指定位置的表面
void* find_surface_at_position(int x, int y, bool* is_wayland) {
    if (!g_compositor_state || !is_wayland) {
        return NULL;
    }
    
    // 先检查Wayland窗口（从顶层开始）
    for (int i = g_compositor_state->wayland_state.window_count - 1; i >= 0; i--) {
        WaylandWindow* window = g_compositor_state->wayland_state.windows[i];
        if (window && window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            *is_wayland = true;
            return window;
        }
    }
    
    // 再检查Xwayland窗口
    for (int i = g_compositor_state->xwayland_state.window_count - 1; i >= 0; i--) {
        XwaylandWindowState* window = g_compositor_state->xwayland_state.windows[i];
        if (window && window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            *is_wayland = false;
            return window;
        }
    }
    
    return NULL;
}

// 处理键盘事件
int compositor_input_event_handle_keyboard(const CompositorInputEvent* event) {
    if (!event || !g_compositor_state) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 获取键盘设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(event->device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_KEYBOARD) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 处理键盘事件
    uint32_t keycode = event->keyboard.keycode;
    CompositorInputState state = event->keyboard.state;
    uint32_t modifiers = event->keyboard.modifiers;
    
    // 更新键盘状态
    if (state == COMPOSITOR_INPUT_STATE_PRESSED) {
        g_compositor_state->keyboard_state[keycode] = 1;
        g_compositor_state->modifiers |= modifiers;
    } else if (state == COMPOSITOR_INPUT_STATE_RELEASED) {
        g_compositor_state->keyboard_state[keycode] = 0;
        g_compositor_state->modifiers &= ~modifiers;
    }
    
    // 如果是捕获模式，直接发送到活动窗口
    if (g_input_capture_mode != COMPOSITOR_INPUT_CAPTURE_NORMAL) {
        if (g_compositor_state->active_surface) {
            // 发送键盘事件到活动窗口
            // 这里需要调用窗口系统的接口发送事件
            return COMPOSITOR_OK;
        }
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 查找目标窗口
    CompositorSurface* target_surface = g_compositor_state->active_surface;
    if (!target_surface) {
        // 如果没有活动窗口，查找鼠标位置的窗口
        bool is_wayland;
        target_surface = find_surface_at_position(event->x, event->y, &is_wayland);
    }
    
    if (target_surface) {
        // 发送键盘事件到目标窗口
        // 这里需要调用窗口系统的接口发送事件
        return COMPOSITOR_OK;
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 处理键盘事件
int process_keyboard_event(int device_id, int key_code, bool pressed, int modifiers) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 获取键盘设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_KEYBOARD) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 创建键盘事件
    CompositorInputEvent event;
    memset(&event, 0, sizeof(event));
    event.type = COMPOSITOR_INPUT_EVENT_KEYBOARD;
    event.device_id = device_id;
    event.keyboard.keycode = key_code;
    event.keyboard.state = pressed ? COMPOSITOR_INPUT_STATE_PRESSED : COMPOSITOR_INPUT_STATE_RELEASED;
    event.keyboard.modifiers = modifiers;
    
    // 处理键盘事件
    return compositor_input_event_handle_keyboard(&event);
}

// 处理鼠标事件
int process_mouse_event(int device_id, int x, int y, int button, bool pressed, int modifiers) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 获取鼠标设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_MOUSE) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 创建鼠标移动事件
    CompositorInputEvent motion_event;
    memset(&motion_event, 0, sizeof(motion_event));
    motion_event.type = COMPOSITOR_INPUT_EVENT_MOUSE_MOTION;
    motion_event.device_id = device_id;
    motion_event.x = x;
    motion_event.y = y;
    
    // 处理鼠标移动事件
    result = compositor_input_event_handle_motion(&motion_event);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    // 如果有按钮状态变化，创建按钮事件
    if (button >= 0) {
        CompositorInputEvent button_event;
        memset(&button_event, 0, sizeof(button_event));
        button_event.type = COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON;
        button_event.device_id = device_id;
        button_event.mouse.button = button;
        button_event.mouse.state = pressed ? COMPOSITOR_INPUT_STATE_PRESSED : COMPOSITOR_INPUT_STATE_RELEASED;
        button_event.mouse.x = x;
        button_event.mouse.y = y;
        
        // 处理鼠标按钮事件
        result = compositor_input_event_handle_button(&button_event);
        if (result != COMPOSITOR_OK) {
            return result;
        }
    }
    
    return COMPOSITOR_OK;
}

// 处理触摸事件
int process_touch_event(int device_id, int touch_id, int x, int y, float pressure, bool pressed, int phase) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 获取触摸设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_TOUCHSCREEN) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 创建触摸事件
    CompositorInputEvent event;
    memset(&event, 0, sizeof(event));
    event.type = COMPOSITOR_INPUT_EVENT_TOUCH;
    event.device_id = device_id;
    event.touch.id = touch_id;
    event.touch.x = x;
    event.touch.y = y;
    event.touch.pressure = pressure;
    event.touch.state = pressed ? COMPOSITOR_INPUT_STATE_PRESSED : COMPOSITOR_INPUT_STATE_RELEASED;
    event.touch.phase = phase;
    
    // 处理触摸事件
    return compositor_input_event_handle_touch(&event);
}

// 处理触摸事件
int compositor_input_event_handle_touch(const CompositorInputEvent* event) {
    if (!event || !g_compositor_state) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 获取触摸设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(event->device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_TOUCHSCREEN) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 处理触摸事件
    int32_t touch_id = event->touch.id;
    float x = event->touch.x;
    float y = event->touch.y;
    float pressure = event->touch.pressure;
    CompositorInputState state = event->touch.state;
    
    // 更新触摸点状态
    if (touch_id >= 0 && touch_id < MAX_TOUCH_POINTS) {
        g_compositor_state->touch_points[touch_id].id = touch_id;
        g_compositor_state->touch_points[touch_id].x = x;
        g_compositor_state->touch_points[touch_id].y = y;
        g_compositor_state->touch_points[touch_id].pressure = pressure;
        g_compositor_state->touch_points[touch_id].state = state;
        
        if (state == COMPOSITOR_INPUT_STATE_PRESSED) {
            g_compositor_state->active_touch_count++;
        } else if (state == COMPOSITOR_INPUT_STATE_RELEASED) {
            if (g_compositor_state->active_touch_count > 0) {
                g_compositor_state->active_touch_count--;
            }
        }
    }
    
    // 如果是捕获模式，直接发送到活动窗口
    if (g_input_capture_mode != COMPOSITOR_INPUT_CAPTURE_NORMAL) {
        if (g_compositor_state->active_surface) {
            // 发送触摸事件到活动窗口
            // 这里需要调用窗口系统的接口发送事件
            return COMPOSITOR_OK;
        }
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 查找目标窗口
    bool is_wayland;
    CompositorSurface* target_surface = find_surface_at_position(x, y, &is_wayland);
    
    if (target_surface) {
        // 更新活动窗口
        g_compositor_state->active_surface = target_surface;
        
        // 发送触摸事件到目标窗口
        // 这里需要调用窗口系统的接口发送事件
        return COMPOSITOR_OK;
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 处理手势事件
int compositor_input_event_handle_gesture(const CompositorInputEvent* event) {
    if (!event || !g_compositor_state) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 处理手势事件
    CompositorGestureType gesture_type = event->gesture.type;
    float x = event->gesture.x;
    float y = event->gesture.y;
    float dx = event->gesture.dx;
    float dy = event->gesture.dy;
    float scale = event->gesture.scale;
    float rotation = event->gesture.rotation;
    int32_t num_fingers = event->gesture.num_fingers;
    
    // 如果是捕获模式，直接发送到活动窗口
    if (g_input_capture_mode != COMPOSITOR_INPUT_CAPTURE_NORMAL) {
        if (g_compositor_state->active_surface) {
            // 发送手势事件到活动窗口
            // 这里需要调用窗口系统的接口发送事件
            return COMPOSITOR_OK;
        }
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 查找目标窗口
    bool is_wayland;
    CompositorSurface* target_surface = find_surface_at_position(x, y, &is_wayland);
    
    if (target_surface) {
        // 更新活动窗口
        g_compositor_state->active_surface = target_surface;
        
        // 发送手势事件到目标窗口
        // 这里需要调用窗口系统的接口发送事件
        return COMPOSITOR_OK;
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 处理鼠标移动事件
int compositor_input_event_handle_motion(const CompositorInputEvent* event) {
    if (!event || !g_compositor_state) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 获取鼠标设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(event->device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_MOUSE) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 更新鼠标位置
    g_compositor_state->mouse_x = event->x;
    g_compositor_state->mouse_y = event->y;
    
    // 如果是捕获模式，直接发送到活动窗口
    if (g_input_capture_mode != COMPOSITOR_INPUT_CAPTURE_NORMAL) {
        if (g_compositor_state->active_surface) {
            // 发送鼠标移动事件到活动窗口
            // 这里需要调用窗口系统的接口发送事件
            return COMPOSITOR_OK;
        }
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 查找目标窗口
    bool is_wayland;
    CompositorSurface* target_surface = find_surface_at_position(event->x, event->y, &is_wayland);
    
    if (target_surface) {
        // 如果鼠标移动到新窗口，更新焦点
        if (g_compositor_state->focused_surface != target_surface) {
            // 发送鼠标离开事件到旧窗口
            if (g_compositor_state->focused_surface) {
                // 这里需要调用窗口系统的接口发送事件
            }
            
            // 发送鼠标进入事件到新窗口
            // 这里需要调用窗口系统的接口发送事件
            
            // 更新焦点窗口
            g_compositor_state->focused_surface = target_surface;
        }
        
        // 发送鼠标移动事件到目标窗口
        // 这里需要调用窗口系统的接口发送事件
        return COMPOSITOR_OK;
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 处理鼠标按钮事件
int compositor_input_event_handle_button(const CompositorInputEvent* event) {
    if (!event || !g_compositor_state) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 获取鼠标设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(event->device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_MOUSE) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 处理鼠标按钮事件
    int button = event->mouse.button;
    CompositorInputState state = event->mouse.state;
    float x = event->mouse.x;
    float y = event->mouse.y;
    
    // 更新鼠标按钮状态
    if (button >= 0 && button < 32) { // 假设最多32个按钮
        if (state == COMPOSITOR_INPUT_STATE_PRESSED) {
            g_compositor_state->mouse_state |= (1 << button);
        } else if (state == COMPOSITOR_INPUT_STATE_RELEASED) {
            g_compositor_state->mouse_state &= ~(1 << button);
        }
    }
    
    // 如果是捕获模式，直接发送到活动窗口
    if (g_input_capture_mode != COMPOSITOR_INPUT_CAPTURE_NORMAL) {
        if (g_compositor_state->active_surface) {
            // 发送鼠标按钮事件到活动窗口
            // 这里需要调用窗口系统的接口发送事件
            return COMPOSITOR_OK;
        }
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 查找目标窗口
    bool is_wayland;
    CompositorSurface* target_surface = find_surface_at_position(x, y, &is_wayland);
    
    if (target_surface) {
        // 更新活动窗口
        g_compositor_state->active_surface = target_surface;
        
        // 发送鼠标按钮事件到目标窗口
        // 这里需要调用窗口系统的接口发送事件
        return COMPOSITOR_OK;
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 处理滚动事件
int compositor_input_event_handle_scroll(const CompositorInputEvent* event) {
    if (!event || !g_compositor_state) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 获取鼠标设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(event->device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_MOUSE) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 处理滚动事件
    float x = event->scroll.x;
    float y = event->scroll.y;
    float dx = event->scroll.dx;
    float dy = event->scroll.dy;
    int32_t axis = event->scroll.axis;
    
    // 如果是捕获模式，直接发送到活动窗口
    if (g_input_capture_mode != COMPOSITOR_INPUT_CAPTURE_NORMAL) {
        if (g_compositor_state->active_surface) {
            // 发送滚动事件到活动窗口
            // 这里需要调用窗口系统的接口发送事件
            return COMPOSITOR_OK;
        }
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 查找目标窗口
    bool is_wayland;
    CompositorSurface* target_surface = find_surface_at_position(x, y, &is_wayland);
    
    if (target_surface) {
        // 更新活动窗口
        g_compositor_state->active_surface = target_surface;
        
        // 发送滚动事件到目标窗口
        // 这里需要调用窗口系统的接口发送事件
        return COMPOSITOR_OK;
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 处理笔事件
int compositor_input_event_handle_pen(const CompositorInputEvent* event) {
    if (!event || !g_compositor_state) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 获取笔设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(event->device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_PEN) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 处理笔事件
    float x = event->x;
    float y = event->y;
    float pressure = event->pressure;
    CompositorInputState state = event->state;
    
    // 更新笔状态
    g_compositor_state->pen_x = x;
    g_compositor_state->pen_y = y;
    g_compositor_state->pen_pressure = pressure;
    g_compositor_state->pen_state = state;
    
    // 如果是捕获模式，直接发送到活动窗口
    if (g_input_capture_mode != COMPOSITOR_INPUT_CAPTURE_NORMAL) {
        if (g_compositor_state->active_surface) {
            // 发送笔事件到活动窗口
            // 这里需要调用窗口系统的接口发送事件
            return COMPOSITOR_OK;
        }
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 查找目标窗口
    bool is_wayland;
    CompositorSurface* target_surface = find_surface_at_position(x, y, &is_wayland);
    
    if (target_surface) {
        // 更新活动窗口
        g_compositor_state->active_surface = target_surface;
        
        // 发送笔事件到目标窗口
        // 这里需要调用窗口系统的接口发送事件
        return COMPOSITOR_OK;
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 处理游戏手柄事件
int compositor_input_event_handle_gamepad(const CompositorInputEvent* event) {
    if (!event || !g_compositor_state) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 获取游戏手柄设备
    CompositorInputDevice device;
    int result = compositor_input_get_device(event->device_id, &device);
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    if (device.type != COMPOSITOR_INPUT_DEVICE_TYPE_GAMEPAD) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 处理游戏手柄事件
    // 这里需要根据事件的具体内容来处理
    // 例如：按钮按下/释放、摇杆移动、扳机变化等
    
    // 如果是捕获模式，直接发送到活动窗口
    if (g_input_capture_mode != COMPOSITOR_INPUT_CAPTURE_NORMAL) {
        if (g_compositor_state->active_surface) {
            // 发送游戏手柄事件到活动窗口
            // 这里需要调用窗口系统的接口发送事件
            return COMPOSITOR_OK;
        }
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 查找目标窗口
    CompositorSurface* target_surface = g_compositor_state->active_surface;
    
    if (target_surface) {
        // 发送游戏手柄事件到目标窗口
        // 这里需要调用窗口系统的接口发送事件
        return COMPOSITOR_OK;
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 处理接近事件
int compositor_input_event_handle_proximity(const CompositorInputEvent* event) {
    if (!event || !g_compositor_state) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 处理接近事件
    // 通常用于笔设备或其他支持接近检测的设备
    
    // 如果是捕获模式，直接发送到活动窗口
    if (g_input_capture_mode != COMPOSITOR_INPUT_CAPTURE_NORMAL) {
        if (g_compositor_state->active_surface) {
            // 发送接近事件到活动窗口
            // 这里需要调用窗口系统的接口发送事件
            return COMPOSITOR_OK;
        }
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 查找目标窗口
    bool is_wayland;
    CompositorSurface* target_surface = find_surface_at_position(event->x, event->y, &is_wayland);
    
    if (target_surface) {
        // 更新活动窗口
        g_compositor_state->active_surface = target_surface;
        
        // 发送接近事件到目标窗口
        // 这里需要调用窗口系统的接口发送事件
        return COMPOSITOR_OK;
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 获取当前输入捕获模式
CompositorInputCaptureMode get_input_capture_mode(void) {
    return g_input_capture_mode;
}

// 设置输入捕获模式
void set_input_capture_mode(CompositorInputCaptureMode mode) {
    g_input_capture_mode = mode;
    log_message(COMPOSITOR_LOG_DEBUG, "Input capture mode set to: %d", mode);
}