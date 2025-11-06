#ifndef COMPOSITOR_INPUT_H
#define COMPOSITOR_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "compositor.h" // 包含基础定义
#include "compositor_window.h" // 包含窗口相关定义
#include "compositor_input_types.h" // 包含输入类型定义

// 前向声明（如果需要）
// struct CompositorState;
typedef struct CompositorState CompositorState;

// 设备类型枚举 - 已移至 compositor_input_types.h

// 输入事件类型枚举 - 已移至 compositor_input_types.h

// 输入状态
enum {
    COMPOSITOR_INPUT_STATE_UP = 0,
    COMPOSITOR_INPUT_STATE_DOWN = 1,
    COMPOSITOR_INPUT_STATE_MOVE = 2
};

// 手势类型枚举 - 已移至 compositor_input_types.h

// 输入性能统计结构体 - 已移至 compositor_input_types.h

// 输入设备信息 - 已移至 compositor_input_types.h

// 触摸点信息 - 已移至 compositor_input_types.h

// 手势信息结构体（扩展） - 已移至 compositor_input_types.h

// 输入捕获模式 - 已移至 compositor_input_types.h

// 手势事件数据 - 已移至 compositor_input_types.h

// 输入事件数据 - 已移至 compositor_input_types.h

// 输入配置结构体 - 已移至 compositor_input_types.h

// 输入状态结构体 - 已移至 compositor_input_types.h

// 注入输入事件（基础接口）
void compositor_handle_input(int type, int x, int y, int key, int state);

// 注入高级输入事件
void compositor_handle_input_event(const CompositorInputEvent* event);

// 注册输入设备
int compositor_register_input_device(const CompositorInputDevice* device);

// 注销输入设备
int compositor_unregister_input_device(int32_t device_id);

// 获取设备列表
int compositor_get_input_devices(CompositorInputDevice** devices, int32_t* count);

// 设置输入捕获模式
void compositor_set_input_capture_mode(bool capture_mouse, bool capture_keyboard);

// 获取当前鼠标位置
void compositor_get_mouse_position(float* x, float* y);

// 设置当前鼠标位置
void compositor_set_mouse_position(float x, float y);

// 显示/隐藏鼠标光标
void compositor_set_cursor_visibility(bool visible);

// 设置鼠标光标样式
void compositor_set_cursor_style(int style);

// 注入滚动事件
void compositor_handle_scroll(float dx, float dy, int device_id);

// 注入手势事件
void compositor_handle_gesture(int gesture_type, float scale, float rotation, float x, float y);

// 设置全局状态指针（内部使用）
void compositor_input_set_state(CompositorState* state);

// 清理输入相关资源
void compositor_input_cleanup(void);

// 手势相关设置函数
void compositor_input_set_gesture_enabled(int gesture_type, bool enabled);
bool compositor_input_is_gesture_enabled(int gesture_type);
void compositor_input_set_gesture_threshold(int gesture_type, float threshold);
float compositor_input_get_gesture_threshold(int gesture_type);

// 高级手势配置
void compositor_input_set_gesture_config(int32_t double_tap_timeout, int32_t long_press_timeout, 
                                        float tap_threshold, float swipe_threshold);

// 触摸相关查询函数
size_t compositor_input_get_active_touch_points(TouchPoint** points);

// 输入事件模拟函数
int compositor_input_simulate_keyboard(int key, int state);
int compositor_input_simulate_mouse_button(int button, int state);
int compositor_input_simulate_mouse_motion(float x, float y);
int compositor_input_simulate_touch(int touch_id, int state, float x, float y, float pressure);

// 游戏控制器配置
void compositor_input_set_gamepad_config(bool enable_mouse_emulation, float sensitivity, 
                                        float deadzone, int max_speed);

// 触控笔配置
void compositor_input_set_pen_config(bool enable_pressure, bool enable_tilt, float pressure_sensitivity);

// 设备查询函数
size_t compositor_input_get_device_count();
CompositorInputDevice** compositor_input_get_all_devices();
bool compositor_input_device_has_capability(int32_t device_id, int capability);

// 设备能力检测
bool compositor_input_is_device_type_supported(int32_t device_type);
bool compositor_input_has_pressure_support(void);
bool compositor_input_has_tilt_support(void);
bool compositor_input_has_rotation_support(void);

// 输入设备管理优化函数
int compositor_input_set_device_priority(CompositorDeviceType type, int priority);
int compositor_input_set_adaptive_mode(bool enabled);
int compositor_input_get_performance_stats(CompositorInputPerformanceStats* stats);



#endif // COMPOSITOR_INPUT_H