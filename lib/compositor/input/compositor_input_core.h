#ifndef COMPOSITOR_INPUT_CORE_H
#define COMPOSITOR_INPUT_CORE_H

#include "compositor.h"
#include "compositor_input_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 全局状态指针
extern CompositorState* g_compositor_state;

// 核心输入处理函数
int compositor_input_core_init(void);
void compositor_input_core_cleanup(void);

// 输入事件处理入口
int compositor_handle_input_event(CompositorInputEvent* event);
void process_input_events(void);

// 设备管理
int compositor_input_register_device(CompositorInputDeviceType type, const char* name, int device_id);
int compositor_input_unregister_device(int device_id);
CompositorInputDevice* compositor_input_get_device(int device_id);
int compositor_input_get_devices(CompositorInputDevice** out_devices, int* out_count);
int compositor_input_set_device_enabled(int device_id, bool enabled);
int compositor_input_set_device_priority(CompositorInputDeviceType type, int priority);
void compositor_input_set_active_device(int device_id);
CompositorInputDevice* compositor_input_get_active_device(void);

// 输入模式控制
int compositor_input_set_adaptive_mode(bool enabled);
void compositor_input_set_capture_mode(CompositorInputCaptureMode mode);
CompositorInputCaptureMode compositor_input_get_capture_mode(void);

// 性能统计
int compositor_input_get_performance_stats(CompositorInputPerformanceStats* stats);

// 设备能力查询
bool compositor_input_is_device_type_supported(CompositorDeviceType device_type);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_INPUT_CORE_H