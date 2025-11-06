#include "compositor_input_dispatcher.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 输入事件处理器
static CompositorInputEventHandler g_event_handler = NULL;
static void* g_event_handler_user_data = NULL;

// 事件统计
static uint32_t g_total_events = 0;
static uint32_t g_events_per_second = 0;
static uint64_t g_last_event_time = 0;
static uint32_t g_events_in_current_second = 0;
static uint64_t g_current_second_start = 0;

// 获取当前时间（毫秒）
static uint64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// 初始化输入事件分发模块
int compositor_input_dispatcher_init(void) {
    g_event_handler = NULL;
    g_event_handler_user_data = NULL;
    g_total_events = 0;
    g_events_per_second = 0;
    g_events_in_current_second = 0;
    g_last_event_time = 0;
    g_current_second_start = get_current_time_ms();
    
    log_message(COMPOSITOR_LOG_DEBUG, "Input dispatcher module initialized");
    return 0;
}

// 清理输入事件分发模块
void compositor_input_dispatcher_cleanup(void) {
    g_event_handler = NULL;
    g_event_handler_user_data = NULL;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Input dispatcher module cleaned up");
}

// 更新事件统计
static void update_event_stats(void) {
    uint64_t current_time = get_current_time_ms();
    
    // 检查是否进入新的一秒
    if (current_time - g_current_second_start >= 1000) {
        g_events_per_second = g_events_in_current_second;
        g_events_in_current_second = 0;
        g_current_second_start = current_time;
    }
    
    g_events_in_current_second++;
    g_total_events++;
    g_last_event_time = current_time;
}

// 分发输入事件
int compositor_input_dispatcher_dispatch_event(const CompositorInputEvent* event) {
    if (!event) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 更新事件统计
    update_event_stats();
    
    // 更新输入性能统计
    compositor_input_performance_update_stats(event);
    
    // 如果设置了事件处理器，调用它
    if (g_event_handler) {
        return g_event_handler(event, g_event_handler_user_data);
    }
    
    return COMPOSITOR_OK;
}

// 模拟鼠标事件
int compositor_input_dispatcher_simulate_mouse_event(int x, int y, int button, bool pressed) {
    CompositorInputEvent event;
    memset(&event, 0, sizeof(event));
    
    event.type = pressed ? COMPOSITOR_EVENT_MOUSE_BUTTON_DOWN : COMPOSITOR_EVENT_MOUSE_BUTTON_UP;
    event.device_type = COMPOSITOR_DEVICE_TYPE_MOUSE;
    event.time = get_current_time_ms();
    
    event.mouse.x = x;
    event.mouse.y = y;
    event.mouse.button = button;
    event.mouse.pressed = pressed;
    
    return compositor_input_dispatcher_dispatch_event(&event);
}

// 模拟键盘事件
int compositor_input_dispatcher_simulate_keyboard_event(int key, bool pressed) {
    CompositorInputEvent event;
    memset(&event, 0, sizeof(event));
    
    event.type = pressed ? COMPOSITOR_EVENT_KEY_DOWN : COMPOSITOR_EVENT_KEY_UP;
    event.device_type = COMPOSITOR_DEVICE_TYPE_KEYBOARD;
    event.time = get_current_time_ms();
    
    event.keyboard.key = key;
    event.keyboard.pressed = pressed;
    
    return compositor_input_dispatcher_dispatch_event(&event);
}

// 设置输入事件处理器
int compositor_input_dispatcher_set_handler(CompositorInputEventHandler handler, void* user_data) {
    g_event_handler = handler;
    g_event_handler_user_data = user_data;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Input event handler set: %p", handler);
    return COMPOSITOR_OK;
}

// 获取输入事件统计
int compositor_input_dispatcher_get_event_stats(uint32_t* total_events, uint32_t* events_per_second) {
    if (!total_events || !events_per_second) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    *total_events = g_total_events;
    *events_per_second = g_events_per_second;
    
    return COMPOSITOR_OK;
}