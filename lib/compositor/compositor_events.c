/*
 * WinDroids Compositor - Event System
 * Implementation of event processing and dispatching
 */

#include "compositor_events.h"
#include "compositor_utils.h"
#include "compositor_input.h"
#include "compositor_dirty.h"
#include <stdlib.h>
#include <string.h>

// 全局状态引用
static CompositorState* g_state = NULL;

// 设置合成器状态引用（内部使用）
void compositor_events_set_state(CompositorState* state) {
    g_state = state;
}

// 初始化事件系统
int compositor_events_init(void) {
    if (!g_state) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Event system initialized");
    return COMPOSITOR_OK;
}

// 清理事件系统
void compositor_events_cleanup(void) {
    if (g_state) {
        log_message(COMPOSITOR_LOG_INFO, "Event system cleaned up");
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

// 处理触摸事件
void process_touch_event(CompositorInputEvent* event) {
    if (!g_state) return;
    
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
    if (!g_state || !g_state->config.enable_gestures) return;
    
    g_state->last_gesture_type = event->gesture.type;
    
    switch (event->gesture.type) {
        case COMPOSITOR_GESTURE_PINCH:
            // 处理缩放手势
            if (g_state->active_window && 
                g_state->config.enable_window_gesture_scaling) {
                float scale_factor = event->gesture.scale;
                // 在实际应用中，这里会调整窗口大小
                if (g_state->config.debug_mode) {
                    log_message(COMPOSITOR_LOG_DEBUG, "Pinch gesture detected, scale: %f", scale_factor);
                }
            }
            break;
            
        case COMPOSITOR_GESTURE_SWIPE:
            // 处理滑动手势
            if (g_state->config.debug_mode) {
                log_message(COMPOSITOR_LOG_DEBUG, "Swipe gesture detected, direction: %d", 
                           event->gesture.direction);
            }
            break;
            
        case COMPOSITOR_GESTURE_DOUBLE_TAP:
            // 处理双击手势（例如最大化窗口）
            if (g_state->active_window && 
                g_state->config.enable_window_double_tap_maximize) {
                if (g_state->config.debug_mode) {
                    log_message(COMPOSITOR_LOG_DEBUG, "Double tap detected on active window");
                }
                // 在实际应用中，这里会处理窗口最大化逻辑
            }
            break;
            
        default:
            break;
    }
}