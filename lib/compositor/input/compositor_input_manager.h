#ifndef COMPOSITOR_INPUT_MANAGER_H
#define COMPOSITOR_INPUT_MANAGER_H

#include "compositor_input_type.h"
#include "compositor_utils.h"

// 初始化输入设备管理模块
int compositor_input_manager_init(void);

// 清理输入设备管理模块
void compositor_input_manager_cleanup(void);

// 注册输入设备
int compositor_input_manager_register_device(CompositorInputDeviceType type, const char* name, int device_id);

// 注销输入设备
int compositor_input_manager_unregister_device(int device_id);

// 获取输入设备
CompositorInputDevice* compositor_input_manager_get_device(int device_id);

// 获取输入设备列表
int compositor_input_manager_get_devices(CompositorInputDevice** out_devices, int* out_count);

// 设置设备启用状态
int compositor_input_manager_set_device_enabled(int device_id, bool enabled);

// 设置输入设备优先级
int compositor_input_manager_set_device_priority(CompositorInputDeviceType type, int priority);

// 获取最高优先级的活动设备
CompositorInputDevice* compositor_input_manager_get_highest_priority_active_device(void);

// 设置活动输入设备
void compositor_input_manager_set_active_device(int device_id);

// 获取活动输入设备
CompositorInputDevice* compositor_input_manager_get_active_device(void);

// 启用/禁用自适应输入处理
int compositor_input_manager_set_adaptive_mode(bool enabled);

// 获取输入捕获模式
CompositorInputCaptureMode compositor_input_manager_get_capture_mode(void);

// 设置输入捕获模式
void compositor_input_manager_set_capture_mode(CompositorInputCaptureMode mode);

// 检查设备类型是否支持
bool compositor_input_manager_is_device_type_supported(CompositorInputDeviceType device_type);

#endif // COMPOSITOR_INPUT_MANAGER_H