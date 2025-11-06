#include "compositor_input.h"
#include "compositor_input_core.h"
#include "compositor_input_window_switch.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 设置手势配置
void compositor_input_set_gesture_config(
    uint32_t double_click_timeout_ms,
    uint32_t long_press_timeout_ms,
    float drag_threshold,
    float scroll_threshold)
{
    // 实际实现中应该通过手势识别模块设置
    // 这里暂时只记录日志
    log_message(COMPOSITOR_LOG_DEBUG, "Setting gesture config: double_click=%ums, long_press=%ums, drag=%.2f, scroll=%.2f",
               double_click_timeout_ms, long_press_timeout_ms, drag_threshold, scroll_threshold);
}

// 初始化输入系统
int compositor_input_init(void)
{
    int result = compositor_input_core_init();
    if (result != COMPOSITOR_OK) {
        return result;
    }
    
    // 初始化窗口切换系统
    result = compositor_input_init_window_switch(NULL);
    if (result != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_WARN, "Failed to initialize window switch system: %d", result);
        // 不返回错误，因为这不是关键功能
    }
    
    return COMPOSITOR_OK;
}

// 清理输入系统
void compositor_input_cleanup(void)
{
    // 清理窗口切换系统
    compositor_input_window_switch_cleanup();
    
    compositor_input_core_cleanup();
}

// 设置输入配置
void compositor_input_set_config(const CompositorInputConfig* config)
{
    if (!config) return;
    
    // 设置手势配置
    compositor_input_set_gesture_config(
        config->double_click_timeout_ms,
        config->long_press_timeout_ms,
        config->drag_threshold,
        config->scroll_threshold
    );
    
    // 设置输入捕获模式
    compositor_input_set_capture_mode(config->enable_gestures ? 
        COMPOSITOR_INPUT_CAPTURE_FULLSCREEN : COMPOSITOR_INPUT_CAPTURE_NORMAL);
}

// 获取输入配置
void compositor_input_get_config(CompositorInputConfig* config)
{
    if (!config) return;
    
    // 获取当前输入捕获模式
    CompositorInputCaptureMode mode = compositor_input_get_capture_mode();
    config->enable_gestures = (mode == COMPOSITOR_INPUT_CAPTURE_FULLSCREEN);
    
    // 获取性能统计
    CompositorInputPerformanceStats stats;
    compositor_input_get_performance_stats(&stats);
    
    // 设置默认值（实际实现中应该从各模块获取）
    config->enable_shortcuts = true;
    config->enable_window_dragging = true;
    config->enable_touch_feedback = true;
    config->double_click_timeout_ms = 300;
    config->long_press_timeout_ms = 500;
    config->drag_threshold = 10.0f;
    config->scroll_threshold = 5.0f;
}

// 处理输入事件
int compositor_input_handle_event(const CompositorInputEvent* event)
{
    return compositor_handle_input_event((CompositorInputEvent*)event);
}

// 获取设备数量
int compositor_input_get_device_count(void)
{
    // 实际实现中应该从设备管理器获取
    return 0;
}

// 获取设备列表
int compositor_input_get_devices(CompositorInputDeviceInfo* devices, int max_devices)
{
    if (!devices || max_devices <= 0) return 0;
    
    // 实际实现中应该从设备管理器获取
    return 0;
}

// 获取设备信息
int compositor_input_get_device_info(int32_t device_id, CompositorInputDeviceInfo* info)
{
    if (!info) return COMPOSITOR_ERROR_INVALID_ARGS;
    
    // 实际实现中应该从设备管理器获取
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 设置设备启用状态
bool compositor_input_set_device_enabled(int32_t device_id, bool enabled)
{
    // 实际实现中应该通过设备管理器设置
    return false;
}

// 设置输入捕获模式
void compositor_input_set_capture_mode(CompositorInputCaptureMode mode)
{
    compositor_input_manager_set_capture_mode(mode);
}

// 获取输入捕获模式
CompositorInputCaptureMode compositor_input_get_capture_mode(void)
{
    return compositor_input_manager_get_capture_mode();
}

// 获取性能统计
void compositor_input_get_performance_stats(CompositorInputPerformanceStats* stats)
{
    if (!stats) return;
    
    compositor_input_core_get_performance_stats(stats);
}

// 重置性能统计
void compositor_input_reset_performance_stats(void)
{
    compositor_input_core_reset_performance_stats();
}

// 设置游戏控制器配置
void compositor_input_set_gamepad_config(float deadzone, float sensitivity)
{
    // 实际实现中应该通过游戏控制器模块设置
}

// 设置触控笔配置
void compositor_input_set_pen_config(float pressure_threshold, float tilt_threshold)
{
    // 实际实现中应该通过触控笔模块设置
}

// 检查压力感应支持
bool compositor_input_has_pressure_support(void)
{
    // 实际实现中应该通过触控笔模块检查
    return false;
}

// 检查倾斜支持
bool compositor_input_has_tilt_support(void)
{
    // 实际实现中应该通过触控笔模块检查
    return false;
}

// 检查旋转支持
bool compositor_input_has_rotation_support(void)
{
    // 实际实现中应该通过触控笔模块检查
    return false;
}

// 模拟输入事件
int compositor_input_simulate_event(const CompositorInputEvent* event)
{
    if (!event) return COMPOSITOR_ERROR_INVALID_ARGS;
    
    // 实际实现中应该通过输入模拟模块处理
    return compositor_handle_input_event((CompositorInputEvent*)event);
}