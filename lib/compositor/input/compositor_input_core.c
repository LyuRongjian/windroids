#include "compositor_input_core.h"
#include "compositor_input_gesture_recognition.h"
#include "compositor_input_window_interaction.h"
#include "compositor_input_shortcuts.h"
#include "compositor_input_simulation.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 全局状态指针
CompositorState* g_compositor_state = NULL;

// 活动设备
static CompositorInputDevice* g_active_device = NULL;

// 输入捕获模式
static CompositorInputCaptureMode g_input_capture_mode = COMPOSITOR_INPUT_CAPTURE_MODE_SURFACE;

// 获取当前时间（毫秒）
static int64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 初始化输入系统核心
int compositor_input_core_init(void)
{
    // 初始化各个模块
    if (compositor_input_manager_init() != 0) {
        return -1;
    }
    
    if (compositor_input_performance_init() != 0) {
        return -1;
    }
    
    if (compositor_input_dispatcher_init() != 0) {
        return -1;
    }
    
    if (compositor_input_device_init() != 0) {
        return -1;
    }
    
    if (compositor_input_event_init() != 0) {
        return -1;
    }
    
    if (compositor_input_gesture_init() != 0) {
        return -1;
    }
    
    if (compositor_input_window_switch_init() != 0) {
        return -1;
    }
    
    if (compositor_input_gamepad_init() != 0) {
        return -1;
    }
    
    // 初始化拆分出的模块
    if (compositor_gesture_recognition_init() != 0) {
        return -1;
    }
    
    if (compositor_window_interaction_init() != 0) {
        return -1;
    }
    
    if (compositor_input_shortcuts_init() != 0) {
        return -1;
    }
    
    if (compositor_input_simulation_init() != 0) {
        return -1;
    }
    
    return 0;
}

// 清理输入系统核心
void compositor_input_core_cleanup(void)
{
    // 清理各个模块
    compositor_input_manager_cleanup();
    compositor_input_performance_cleanup();
    compositor_input_dispatcher_cleanup();
    compositor_input_device_cleanup();
    compositor_input_event_cleanup();
    compositor_input_gesture_cleanup();
    compositor_input_window_switch_cleanup();
    compositor_input_gamepad_cleanup();
    
    // 清理拆分出的模块
    compositor_gesture_recognition_cleanup();
    compositor_window_interaction_cleanup();
    compositor_input_shortcuts_cleanup();
    compositor_input_simulation_cleanup();
    
    // 重置状态
    g_compositor_state = NULL;
    g_active_device = NULL;
    g_input_capture_mode = COMPOSITOR_INPUT_CAPTURE_MODE_SURFACE;
}

// 处理输入事件
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
        // 设置活动设备
        g_active_device = device;
    }
    
    // 根据输入捕获模式过滤事件
    if (g_input_capture_mode == COMPOSITOR_INPUT_CAPTURE_MODE_DISABLED) {
        return COMPOSITOR_OK;  // 输入已禁用
    }
    
    // 性能优化：批量事件处理检测
    static int64_t last_event_time = 0;
    static int event_batch_count = 0;
    int64_t current_time = get_current_time_ms();
    
    // 事件批处理计数
    if (current_time - last_event_time < 5) {  // 5ms内的事件视为同一批次
        event_batch_count++;
    } else {
        event_batch_count = 1;
    }
    last_event_time = current_time;
    
    // 调试日志 - 减少高频事件的日志输出
    if (event->type != COMPOSITOR_INPUT_EVENT_MOUSE_MOTION || event_batch_count % 10 == 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "Handling input event: type=%d, device_id=%d", 
                   event->type, event->device_id);
    }
    
    // 更新全局鼠标位置
    if (event->type == COMPOSITOR_INPUT_EVENT_MOUSE_MOTION || 
        event->type == COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON ||
        event->type == COMPOSITOR_INPUT_EVENT_PEN) {
        g_compositor_state->mouse_x = event->x;
        g_compositor_state->mouse_y = event->y;
    }
    
    // 处理不同类型的输入事件
    switch (event->type) {
        case COMPOSITOR_INPUT_EVENT_MOUSE_MOTION:
            process_mouse_motion_event(event);
            break;
            
        case COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON:
            process_mouse_button_event(event);
            break;
            
        case COMPOSITOR_INPUT_EVENT_KEYBOARD:
            process_keyboard_event(event);
            break;
            
        case COMPOSITOR_INPUT_EVENT_TOUCH:
            process_touch_event(event);
            break;
            
        case COMPOSITOR_INPUT_EVENT_GESTURE:
            process_gesture_event(event);
            break;
            
        case COMPOSITOR_INPUT_EVENT_GAMEPAD:
            process_gamepad_event(event);
            break;
            
        case COMPOSITOR_INPUT_EVENT_PEN:
            process_pen_event(event);
            break;
            
        default:
            if (g_compositor_state && g_compositor_state->config.debug_mode) {
                log_message(COMPOSITOR_LOG_DEBUG, "Unhandled input event type: %d", event->type);
            }
            break;
    }
    
    return COMPOSITOR_OK;
}

// 处理输入事件
void process_input_events() {
    if (!g_compositor_state) return;
    
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
                if (g_compositor_state && g_compositor_state->config.debug_mode) {
                    log_message(COMPOSITOR_LOG_DEBUG, "Unhandled input event type: %d", event.type);
                }
                break;
        }
    }
    
    // 更新光标动画和自动隐藏
    if (g_cursor_state.initialized) {
        static uint64_t last_update_time = 0;
        uint64_t current_time = get_current_time_ms();
        
        if (last_update_time > 0) {
            float delta_time = (current_time - last_update_time) / 1000.0f;
            compositor_cursor_update(delta_time);
        }
        
        last_update_time = current_time;
    }
}

// 注册输入设备
int compositor_input_register_device(CompositorInputDeviceType type, const char* name, int device_id) {
    return compositor_input_manager_register_device(type, name, device_id);
}

// 注销输入设备
int compositor_input_unregister_device(int device_id) {
    return compositor_input_manager_unregister_device(device_id);
}

// 获取输入设备
CompositorInputDevice* compositor_input_get_device(int device_id) {
    return compositor_input_manager_get_device(device_id);
}

// 获取输入设备列表
int compositor_input_get_devices(CompositorInputDevice** out_devices, int* out_count) {
    return compositor_input_manager_get_devices(out_devices, out_count);
}

// 设置设备启用状态
int compositor_input_set_device_enabled(int device_id, bool enabled) {
    return compositor_input_manager_set_device_enabled(device_id, enabled);
}

// 设置输入设备优先级
int compositor_input_set_device_priority(CompositorInputDeviceType type, int priority) {
    return compositor_input_manager_set_device_priority(type, priority);
}

// 设置活动输入设备
void compositor_input_set_active_device(int device_id) {
    compositor_input_manager_set_active_device(device_id);
}

// 获取活动输入设备
CompositorInputDevice* compositor_input_get_active_device(void) {
    return compositor_input_manager_get_active_device();
}

// 启用/禁用自适应输入处理
int compositor_input_set_adaptive_mode(bool enabled) {
    return compositor_input_manager_set_adaptive_mode(enabled);
}

// 设置输入捕获模式
void compositor_input_set_capture_mode(CompositorInputCaptureMode mode) {
    g_input_capture_mode = mode;
    compositor_input_manager_set_capture_mode(mode);
}

// 获取输入捕获模式
CompositorInputCaptureMode compositor_input_get_capture_mode(void) {
    return g_input_capture_mode;
}

// 获取输入性能统计
int compositor_input_get_performance_stats(CompositorInputPerformanceStats* stats) {
    return compositor_input_performance_get_stats(stats);
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

// 获取设备能力
bool compositor_input_is_device_type_supported(CompositorDeviceType device_type) {
    return compositor_input_manager_is_device_type_supported(device_type);
}