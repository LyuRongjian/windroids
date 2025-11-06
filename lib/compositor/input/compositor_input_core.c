#include "compositor_input_core.h"
#include "compositor_input_gesture_recognition.h"
#include "compositor_input_window_interaction.h"
#include "compositor_input_shortcuts.h"
#include "compositor_input_simulation.h"
#include "compositor_input_pen.h"
#include "compositor_input_window_switch.h"
#include "compositor_window_preview.h"
#include "compositor_module_utils.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 全局状态指针
CompositorState* g_compositor_state = NULL;

// 活动设备
static CompositorInputDevice* g_active_device = NULL;

// 输入捕获模式
static CompositorInputCaptureMode g_input_capture_mode = COMPOSITOR_INPUT_CAPTURE_MODE_SURFACE;

// 事件队列
#define MAX_EVENT_QUEUE_SIZE 256
static CompositorInputEvent g_event_queue[MAX_EVENT_QUEUE_SIZE];
static int g_event_queue_head = 0;
static int g_event_queue_tail = 0;
static bool g_event_queue_full = false;

// 获取当前时间（毫秒）
static int64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 向事件队列添加事件
static bool enqueue_event(const CompositorInputEvent* event) {
    if (!event) {
        return false;
    }
    
    if (g_event_queue_full) {
        log_message(COMPOSITOR_LOG_WARN, "Event queue is full, dropping event");
        return false;
    }
    
    // 复制事件到队列
    g_event_queue[g_event_queue_tail] = *event;
    
    // 更新尾指针
    g_event_queue_tail = (g_event_queue_tail + 1) % MAX_EVENT_QUEUE_SIZE;
    
    // 检查队列是否已满
    if (g_event_queue_tail == g_event_queue_head) {
        g_event_queue_full = true;
    }
    
    return true;
}

// 从事件队列获取事件
static bool dequeue_event(CompositorInputEvent* out_event) {
    if (!out_event) {
        return false;
    }
    
    // 检查队列是否为空
    if (g_event_queue_head == g_event_queue_tail && !g_event_queue_full) {
        return false;
    }
    
    // 复制事件
    *out_event = g_event_queue[g_event_queue_head];
    
    // 更新头指针
    g_event_queue_head = (g_event_queue_head + 1) % MAX_EVENT_QUEUE_SIZE;
    
    // 更新队列状态
    g_event_queue_full = false;
    
    return true;
}

// 获取性能统计
int compositor_input_core_get_performance_stats(CompositorInputPerformanceStats* stats) {
    if (!stats) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    return compositor_input_performance_get_stats(stats);
}

// 重置性能统计
int compositor_input_core_reset_performance_stats(void) {
    compositor_input_performance_reset_stats();
    return COMPOSITOR_OK;
}

// 获取下一个输入事件
bool compositor_input_get_next_event(CompositorInputEvent* out_event, int timeout_ms) {
    if (!out_event) {
        return false;
    }
    
    // 首先尝试从队列中获取事件
    if (dequeue_event(out_event)) {
        return true;
    }
    
    // 如果队列为空且设置了超时，等待指定时间
    if (timeout_ms > 0) {
        // 简单实现：直接返回false，不进行等待
        // 在实际实现中，这里可以使用条件变量或其他同步机制
        return false;
    }
    
    return false;
}

// 初始化输入系统核心
int compositor_input_core_init(void)
{
    // 定义所有需要初始化的模块
    static const ModuleInfo input_modules[] = {
        DEFINE_MODULE("input_manager", compositor_input_manager_init, compositor_input_manager_cleanup),
        DEFINE_MODULE("input_performance", compositor_input_performance_init, compositor_input_performance_cleanup),
        DEFINE_MODULE("input_dispatcher", compositor_input_dispatcher_init, compositor_input_dispatcher_cleanup),
        DEFINE_MODULE("input_device", compositor_input_device_init, compositor_input_device_cleanup),
        DEFINE_MODULE("input_event", compositor_input_event_init, compositor_input_event_cleanup),
        DEFINE_MODULE("input_gesture", compositor_input_gesture_init, compositor_input_gesture_cleanup),
        DEFINE_MODULE("input_window_switch", compositor_input_window_switch_init, compositor_input_window_switch_cleanup),
        DEFINE_MODULE("input_gamepad", compositor_input_gamepad_init, compositor_input_gamepad_cleanup),
        DEFINE_MODULE("gesture_recognition", compositor_gesture_recognition_init, compositor_gesture_recognition_cleanup),
        DEFINE_MODULE("window_interaction", compositor_window_interaction_init, compositor_window_interaction_cleanup),
        DEFINE_MODULE("input_shortcuts", compositor_input_shortcuts_init, compositor_input_shortcuts_cleanup),
        DEFINE_MODULE("input_simulation", compositor_input_simulation_init, compositor_input_simulation_cleanup),
        DEFINE_MODULE("input_pen", compositor_input_pen_init, compositor_input_pen_cleanup),
    };
    
    // 批量初始化所有模块
    return compositor_init_modules(input_modules, sizeof(input_modules) / sizeof(input_modules[0]));
}

// 清理输入系统核心
void compositor_input_core_cleanup(void)
{
    // 定义所有需要清理的模块
    static const ModuleInfo input_modules[] = {
        DEFINE_MODULE("input_manager", compositor_input_manager_init, compositor_input_manager_cleanup),
        DEFINE_MODULE("input_performance", compositor_input_performance_init, compositor_input_performance_cleanup),
        DEFINE_MODULE("input_dispatcher", compositor_input_dispatcher_init, compositor_input_dispatcher_cleanup),
        DEFINE_MODULE("input_device", compositor_input_device_init, compositor_input_device_cleanup),
        DEFINE_MODULE("input_event", compositor_input_event_init, compositor_input_event_cleanup),
        DEFINE_MODULE("input_gesture", compositor_input_gesture_init, compositor_input_gesture_cleanup),
        DEFINE_MODULE("input_window_switch", compositor_input_window_switch_init, compositor_input_window_switch_cleanup),
        DEFINE_MODULE("input_gamepad", compositor_input_gamepad_init, compositor_input_gamepad_cleanup),
        DEFINE_MODULE("gesture_recognition", compositor_gesture_recognition_init, compositor_gesture_recognition_cleanup),
        DEFINE_MODULE("window_interaction", compositor_window_interaction_init, compositor_window_interaction_cleanup),
        DEFINE_MODULE("input_shortcuts", compositor_input_shortcuts_init, compositor_input_shortcuts_cleanup),
        DEFINE_MODULE("input_simulation", compositor_input_simulation_init, compositor_input_simulation_cleanup),
        DEFINE_MODULE("input_pen", compositor_input_pen_init, compositor_input_pen_cleanup),
    };
    
    // 批量清理所有模块
    compositor_cleanup_modules(input_modules, sizeof(input_modules) / sizeof(input_modules[0]), -1);
    
    // 重置状态
    g_compositor_state = NULL;
    g_active_device = NULL;
    g_input_capture_mode = COMPOSITOR_INPUT_CAPTURE_MODE_SURFACE;
    
    // 重置事件队列
    g_event_queue_head = 0;
    g_event_queue_tail = 0;
    g_event_queue_full = false;
}

// 处理输入事件
int compositor_handle_input_event(CompositorInputEvent* event) {
    // 错误处理和边界检查
    if (!g_compositor_state || !event) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 检查事件类型的有效性
    if (event->type < 0 || event->type >= COMPOSITOR_INPUT_EVENT_MAX) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid event type: %d", event->type);
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 检查坐标边界
    if (event->x < 0 || event->y < 0 || 
        event->x > g_compositor_state->width || 
        event->y > g_compositor_state->height) {
        log_message(COMPOSITOR_LOG_WARNING, "Event coordinates out of bounds: (%d, %d)", event->x, event->y);
        // 继续处理但记录警告
    }
    
    // 检查输入设备是否启用
    if (event->device_id != -1) {
        CompositorInputDevice* device = compositor_input_get_device(event->device_id);
        if (!device) {
            log_message(COMPOSITOR_LOG_WARNING, "Unknown device ID: %d", event->device_id);
            return COMPOSITOR_ERROR_INVALID_ARGS;
        }
        if (!device->enabled) {
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
        // 防止计数器溢出
        if (event_batch_count > 1000) {
            event_batch_count = 100;
        }
    } else {
        event_batch_count = 1;
    }
    last_event_time = current_time;
    
    // 调试日志 - 减少高频事件的日志输出
    if (event->type != COMPOSITOR_INPUT_EVENT_MOUSE_MOTION || event_batch_count % 10 == 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "Handling input event: type=%d, device_id=%d", 
                   event->type, event->device_id);
    }
    
    // 更新全局鼠标位置 - 添加边界检查
    if (event->type == COMPOSITOR_INPUT_EVENT_MOUSE_MOTION || 
        event->type == COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON ||
        event->type == COMPOSITOR_INPUT_EVENT_PEN) {
        // 确保坐标在有效范围内
        g_compositor_state->mouse_x = (event->x >= 0 && event->x <= g_compositor_state->width) ? 
                                     event->x : g_compositor_state->mouse_x;
        g_compositor_state->mouse_y = (event->y >= 0 && event->y <= g_compositor_state->height) ? 
                                     event->y : g_compositor_state->mouse_y;
    }
    
    // 首先处理窗口切换和预览功能
    if (compositor_input_window_switch_handle_event(event) == COMPOSITOR_OK) {
        // 事件已被窗口切换系统处理
        return COMPOSITOR_OK;
    }
    
    // 处理不同类型的输入事件
    switch (event->type) {
        case COMPOSITOR_INPUT_EVENT_MOUSE_MOTION:
            if (process_mouse_motion_event) {
                process_mouse_motion_event(event);
            } else {
                log_message(COMPOSITOR_LOG_ERROR, "Mouse motion handler not initialized");
                return COMPOSITOR_ERROR_NOT_INITIALIZED;
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON:
            if (process_mouse_button_event) {
                process_mouse_button_event(event);
            } else {
                log_message(COMPOSITOR_LOG_ERROR, "Mouse button handler not initialized");
                return COMPOSITOR_ERROR_NOT_INITIALIZED;
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_KEYBOARD:
            if (process_keyboard_event) {
                process_keyboard_event(event);
            } else {
                log_message(COMPOSITOR_LOG_ERROR, "Keyboard handler not initialized");
                return COMPOSITOR_ERROR_NOT_INITIALIZED;
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_TOUCH:
            if (process_touch_event) {
                process_touch_event(event);
            } else {
                log_message(COMPOSITOR_LOG_ERROR, "Touch handler not initialized");
                return COMPOSITOR_ERROR_NOT_INITIALIZED;
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_GESTURE:
            if (process_gesture_event) {
                process_gesture_event(event);
            } else {
                log_message(COMPOSITOR_LOG_ERROR, "Gesture handler not initialized");
                return COMPOSITOR_ERROR_NOT_INITIALIZED;
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_GAMEPAD:
            if (process_gamepad_event) {
                process_gamepad_event(event);
            } else {
                log_message(COMPOSITOR_LOG_ERROR, "Gamepad handler not initialized");
                return COMPOSITOR_ERROR_NOT_INITIALIZED;
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_PEN:
            if (process_pen_event) {
                process_pen_event(event);
            } else {
                log_message(COMPOSITOR_LOG_ERROR, "Pen handler not initialized");
                return COMPOSITOR_ERROR_NOT_INITIALIZED;
            }
            break;
            
        default:
            if (g_compositor_state && g_compositor_state->config.debug_mode) {
                log_message(COMPOSITOR_LOG_DEBUG, "Unhandled input event type: %d", event->type);
            }
            return COMPOSITOR_ERROR_UNSUPPORTED;
    }
    
    return COMPOSITOR_OK;
}

// 处理输入事件
void process_input_events(int timeout_ms) {
    if (!g_compositor_state) return;
    
    CompositorInputEvent event;
    while (compositor_input_get_next_event(&event, timeout_ms)) {
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
bool compositor_input_is_device_type_supported(CompositorInputDeviceType device_type) {
    return compositor_input_manager_is_device_type_supported(device_type);
}