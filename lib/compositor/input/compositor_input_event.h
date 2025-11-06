/*
 * WinDroids Compositor
 * Input Event Handling Module
 */

#ifndef COMPOSITOR_INPUT_EVENT_H
#define COMPOSITOR_INPUT_EVENT_H

#include "compositor.h"
#include "compositor_input_type.h"
#include "compositor_input_device.h"
#include "compositor_utils.h"
#include <stdbool.h>

// 事件处理函数声明

// 设置合成器状态引用（供内部使用）
void compositor_input_event_set_state(CompositorState* state);

// 初始化事件处理系统
int compositor_input_event_init(void);

// 清理事件处理系统
void compositor_input_event_cleanup(void);

// 处理窗口事件
void process_window_events(CompositorState* state);

// 查找指定位置的表面
void* find_surface_at_position(int x, int y, bool* out_is_wayland);

// 处理键盘事件
int process_keyboard_event(int device_id, int key_code, bool pressed, int modifiers);

// 处理鼠标事件
int process_mouse_event(int device_id, int x, int y, int button, bool pressed, int modifiers);

// 处理触摸事件
int process_touch_event(int device_id, int touch_id, int x, int y, float pressure, bool pressed, int phase);

// 获取当前输入捕获模式
CompositorInputCaptureMode get_input_capture_mode(void);

// 设置输入捕获模式
void set_input_capture_mode(CompositorInputCaptureMode mode);

// 处理键盘事件
int compositor_input_event_handle_keyboard(const CompositorInputEvent* event);

// 处理鼠标事件
int compositor_input_event_handle_mouse(const CompositorInputEvent* event);

// 处理触摸事件
int compositor_input_event_handle_touch(const CompositorInputEvent* event);

// 处理手势事件
int compositor_input_event_handle_gesture(const CompositorInputEvent* event);

// 处理运动事件
int compositor_input_event_handle_motion(const CompositorInputEvent* event);

// 处理按钮事件
int compositor_input_event_handle_button(const CompositorInputEvent* event);

// 处理滚动事件
int compositor_input_event_handle_scroll(const CompositorInputEvent* event);

// 处理笔事件
int compositor_input_event_handle_pen(const CompositorInputEvent* event);

// 处理游戏手柄事件
int compositor_input_event_handle_gamepad(const CompositorInputEvent* event);

// 处理接近事件
int compositor_input_event_handle_proximity(const CompositorInputEvent* event);

#endif // COMPOSITOR_INPUT_EVENT_H