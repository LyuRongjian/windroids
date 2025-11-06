#ifndef COMPOSITOR_INPUT_DEVICE_UTILS_H
#define COMPOSITOR_INPUT_DEVICE_UTILS_H

#include "compositor_input_type.h"
#include "compositor_utils.h"
#include <stdbool.h>
#include <stdint.h>

// 设备查找结果结构体
typedef struct {
    CompositorInputDevice* device;
    int index;
    bool found;
} DeviceSearchResult;

// 设备迭代回调函数类型
typedef bool (*DeviceIteratorCallback)(CompositorInputDevice* device, void* user_data);

// 设备配置结构体
typedef struct {
    bool device_type_supported[10];
    int max_simultaneous_touches;
    int device_priority[10];
    bool adaptive_input;
    int input_response_time;
} DeviceConfig;

// 设备能力标志
typedef enum {
    DEVICE_CAPABILITY_NONE = 0,
    DEVICE_CAPABILITY_PRESSURE = (1 << 0),
    DEVICE_CAPABILITY_TILT = (1 << 1),
    DEVICE_CAPABILITY_ROTATION = (1 << 2),
    DEVICE_CAPABILITY_HOVER = (1 << 3),
    DEVICE_CAPABILITY_MULTI_TOUCH = (1 << 4),
    DEVICE_CAPABILITY_GESTURE = (1 << 5)
} DeviceCapability;

// 设备优先级枚举
typedef enum {
    DEVICE_PRIORITY_LOWEST = 1,
    DEVICE_PRIORITY_LOW = 3,
    DEVICE_PRIORITY_MEDIUM = 5,
    DEVICE_PRIORITY_HIGH = 7,
    DEVICE_PRIORITY_HIGHER = 8,
    DEVICE_PRIORITY_HIGHEST = 9
} DevicePriority;

// 设备类型到优先级的默认映射
typedef struct {
    CompositorInputDeviceType type;
    DevicePriority priority;
} DeviceTypePriorityMap;

// 设备查找函数
DeviceSearchResult compositor_input_device_find_by_id(int device_id);
DeviceSearchResult compositor_input_device_find_by_name(const char* name);
DeviceSearchResult compositor_input_device_find_by_type(CompositorInputDeviceType type);
CompositorInputDevice* compositor_input_device_find_highest_priority(void);

// 设备迭代函数
bool compositor_input_device_for_each(DeviceIteratorCallback callback, void* user_data);
int compositor_input_device_count_by_type(CompositorInputDeviceType type);
int compositor_input_device_count_enabled(void);

// 设备优先级管理
void compositor_input_device_set_default_priorities(void);
int compositor_input_device_get_default_priority(CompositorInputDeviceType type);
void compositor_input_device_update_priorities_by_type(CompositorInputDeviceType type, int priority);

// 设备状态检查
bool compositor_input_device_is_active(const CompositorInputDevice* device);
bool compositor_input_device_is_enabled(const CompositorInputDevice* device);
bool compositor_input_device_has_capability(const CompositorInputDevice* device, DeviceCapability capability);

// 设备配置管理
void compositor_input_device_init_config(DeviceConfig* config);
void compositor_input_device_copy_config(const DeviceConfig* src, DeviceConfig* dst);
bool compositor_input_device_is_type_supported(const DeviceConfig* config, CompositorInputDeviceType type);
int compositor_input_device_get_type_priority(const DeviceConfig* config, CompositorInputDeviceType type);
void compositor_input_device_set_type_priority(DeviceConfig* config, CompositorInputDeviceType type, int priority);

// 设备能力管理
uint32_t compositor_input_device_get_default_capabilities(CompositorInputDeviceType type);
bool compositor_input_device_supports_capability(CompositorInputDeviceType type, DeviceCapability capability);
void compositor_input_device_add_capability(CompositorInputDevice* device, DeviceCapability capability);
void compositor_input_device_remove_capability(CompositorInputDevice* device, DeviceCapability capability);

// 设备创建和销毁辅助函数
CompositorInputDevice* compositor_input_device_create(CompositorInputDeviceType type, const char* name, int device_id);
void compositor_input_device_destroy(CompositorInputDevice* device);
void compositor_input_device_copy(const CompositorInputDevice* src, CompositorInputDevice* dst);

// 设备验证函数
bool compositor_input_device_is_valid(const CompositorInputDevice* device);
bool compositor_input_device_is_valid_type(CompositorInputDeviceType type);
bool compositor_input_device_is_valid_id(int device_id);

// 设备调试和日志
void compositor_input_device_log_info(const CompositorInputDevice* device);
void compositor_input_device_log_error(const char* message, const CompositorInputDevice* device);
void compositor_input_device_log_debug(const char* message, const CompositorInputDevice* device);

#endif // COMPOSITOR_INPUT_DEVICE_UTILS_H