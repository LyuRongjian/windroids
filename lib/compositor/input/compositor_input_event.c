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
int process_keyboard_event(int device_id, int key_code, bool pressed, int modifiers) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 这里实现键盘事件处理逻辑
    // 例如：处理快捷键、传递给活动窗口等
    
    log_message(COMPOSITOR_LOG_DEBUG, "Keyboard event: device=%d, key=%d, pressed=%d, modifiers=%d",
               device_id, key_code, pressed, modifiers);
    
    return COMPOSITOR_OK;
}

// 处理鼠标事件
int process_mouse_event(int device_id, int x, int y, int button, bool pressed, int modifiers) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 这里实现鼠标事件处理逻辑
    // 例如：查找点击的窗口、处理窗口拖动等
    
    log_message(COMPOSITOR_LOG_DEBUG, "Mouse event: device=%d, x=%d, y=%d, button=%d, pressed=%d, modifiers=%d",
               device_id, x, y, button, pressed, modifiers);
    
    return COMPOSITOR_OK;
}

// 处理触摸事件
int process_touch_event(int device_id, int touch_id, int x, int y, float pressure, bool pressed, int phase) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 这里实现触摸事件处理逻辑
    // 通常会将触摸事件转换为手势识别系统进行处理
    
    log_message(COMPOSITOR_LOG_DEBUG, "Touch event: device=%d, touch_id=%d, x=%d, y=%d, pressure=%f, pressed=%d, phase=%d",
               device_id, touch_id, x, y, pressure, pressed, phase);
    
    return COMPOSITOR_OK;
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