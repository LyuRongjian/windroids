#ifndef COMPOSITOR_INPUT_DISPATCHER_H
#define COMPOSITOR_INPUT_DISPATCHER_H

#include "compositor_input.h"
#include "compositor_input_manager.h"
#include "compositor_input_performance.h"
#include "compositor_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化输入事件分发模块
int compositor_input_dispatcher_init(void);

// 清理输入事件分发模块
void compositor_input_dispatcher_cleanup(void);

// 分发输入事件
int compositor_input_dispatcher_dispatch_event(const CompositorInputEvent* event);

// 模拟鼠标事件
int compositor_input_dispatcher_simulate_mouse_event(int x, int y, int button, bool pressed);

// 模拟键盘事件
int compositor_input_dispatcher_simulate_keyboard_event(int key, bool pressed);

// 设置输入事件处理器
typedef int (*CompositorInputEventHandler)(const CompositorInputEvent* event, void* user_data);

int compositor_input_dispatcher_set_handler(CompositorInputEventHandler handler, void* user_data);

// 获取输入事件统计
int compositor_input_dispatcher_get_event_stats(uint32_t* total_events, uint32_t* events_per_second);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_INPUT_DISPATCHER_H