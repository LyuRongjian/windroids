/*
 * WinDroids Compositor - Event System
 * Handles event processing, dispatching, and event queue management
 */

#ifndef COMPOSITOR_EVENTS_H
#define COMPOSITOR_EVENTS_H

#include "compositor.h"

// 设置合成器状态引用（内部使用）
void compositor_events_set_state(CompositorState* state);

// 处理窗口事件
void process_window_events(CompositorState* state);

// 处理触摸事件
void process_touch_event(CompositorInputEvent* event);

// 处理手势事件
void process_gesture_event(CompositorInputEvent* event);

// 初始化事件系统
int compositor_events_init(void);

// 清理事件系统
void compositor_events_cleanup(void);

#endif // COMPOSITOR_EVENTS_H