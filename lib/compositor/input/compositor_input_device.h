/*
 * WinDroids Compositor
 * Input Device Management
 */

#ifndef COMPOSITOR_INPUT_DEVICE_H
#define COMPOSITOR_INPUT_DEVICE_H

#include "compositor_input_type.h"
#include "compositor_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

// 最大触摸点数
#define MAX_TOUCH_POINTS 10

// 设备管理函数声明

// 设置合成器状态引用（供内部使用）
void compositor_input_device_set_state(CompositorState* state);

// 初始化设备管理系统
int compositor_input_device_init(void);

// 清理设备管理系统
void compositor_input_device_cleanup(void);

// 注册输入设备
int compositor_input_register_device(CompositorInputDeviceType type, const char* name, int device_id);

// 注销输入设备
int compositor_input_unregister_device(int device_id);

// 启用/禁用输入设备
int compositor_input_enable_device(int device_id, bool enabled);

// 获取设备状态
int compositor_input_get_device(int device_id, CompositorInputDevice* device);

// 获取设备数量
int compositor_input_get_device_count(void);

// 获取设备列表
int compositor_input_get_devices(CompositorInputDevice** devices, int* count, int max_count);

// 设置设备特定配置
int compositor_input_set_device_config(int device_id, void* config);

// 获取设备特定配置
int compositor_input_get_device_config(int device_id, void** config);

// 获取活动输入设备
int compositor_input_get_active_device(CompositorInputDevice* device);

// 设置活动输入设备
int compositor_input_set_active_device(int device_id);

// 设备优先级管理
int compositor_input_device_set_priority(int device_id, int priority);
int compositor_input_device_get_priority(int device_id, int* priority);

// 获取设备信息
int compositor_input_device_get_info(int device_id, CompositorInputDevice* info);

// 获取设备状态
int compositor_input_device_get_status(int device_id, bool* enabled);

// 获取活动设备
int compositor_input_device_get_active(CompositorInputDevice* device);

// 获取所有设备
int compositor_input_device_get_all(CompositorInputDevice** devices, int* count, int max_count);

// 获取指定类型的设备
int compositor_input_device_get_by_type(CompositorInputDeviceType type, CompositorInputDevice** devices, int* count, int max_count);

#endif // COMPOSITOR_INPUT_DEVICE_H