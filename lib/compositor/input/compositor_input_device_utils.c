#include "compositor_input_device_utils.h"
#include "compositor_input_manager.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 设备类型到优先级的默认映射表
static const DeviceTypePriorityMap default_priority_map[] = {
    {COMPOSITOR_INPUT_DEVICE_TYPE_KEYBOARD, DEVICE_PRIORITY_HIGHEST},
    {COMPOSITOR_INPUT_DEVICE_TYPE_MOUSE, DEVICE_PRIORITY_HIGHER},
    {COMPOSITOR_INPUT_DEVICE_TYPE_TOUCHSCREEN, DEVICE_PRIORITY_HIGH},
    {COMPOSITOR_INPUT_DEVICE_TYPE_PEN, DEVICE_PRIORITY_HIGH},
    {COMPOSITOR_INPUT_DEVICE_TYPE_GAMEPAD, DEVICE_PRIORITY_MEDIUM},
    {COMPOSITOR_INPUT_DEVICE_TYPE_UNKNOWN, DEVICE_PRIORITY_LOW}
};

// 设备类型到默认能力的映射表
static const struct {
    CompositorInputDeviceType type;
    uint32_t capabilities;
} default_capabilities_map[] = {
    {COMPOSITOR_INPUT_DEVICE_TYPE_MOUSE, DEVICE_CAPABILITY_NONE},
    {COMPOSITOR_INPUT_DEVICE_TYPE_KEYBOARD, DEVICE_CAPABILITY_NONE},
    {COMPOSITOR_INPUT_DEVICE_TYPE_TOUCHSCREEN, DEVICE_CAPABILITY_MULTI_TOUCH | DEVICE_CAPABILITY_GESTURE},
    {COMPOSITOR_INPUT_DEVICE_TYPE_PEN, DEVICE_CAPABILITY_PRESSURE | DEVICE_CAPABILITY_TILT | DEVICE_CAPABILITY_ROTATION | DEVICE_CAPABILITY_HOVER},
    {COMPOSITOR_INPUT_DEVICE_TYPE_GAMEPAD, DEVICE_CAPABILITY_NONE},
    {COMPOSITOR_INPUT_DEVICE_TYPE_UNKNOWN, DEVICE_CAPABILITY_NONE}
};

// 设备查找函数
DeviceSearchResult compositor_input_device_find_by_id(int device_id) {
    DeviceSearchResult result = {0};
    
    CompositorInputDevice* devices = NULL;
    int count = 0;
    
    if (compositor_input_manager_get_devices(&devices, &count) == COMPOSITOR_OK) {
        for (int i = 0; i < count; i++) {
            if (devices[i].device_id == device_id) {
                result.device = &devices[i];
                result.index = i;
                result.found = true;
                break;
            }
        }
    }
    
    return result;
}

DeviceSearchResult compositor_input_device_find_by_name(const char* name) {
    DeviceSearchResult result = {0};
    
    if (!name) {
        return result;
    }
    
    CompositorInputDevice* devices = NULL;
    int count = 0;
    
    if (compositor_input_manager_get_devices(&devices, &count) == COMPOSITOR_OK) {
        for (int i = 0; i < count; i++) {
            if (devices[i].name && strcmp(devices[i].name, name) == 0) {
                result.device = &devices[i];
                result.index = i;
                result.found = true;
                break;
            }
        }
    }
    
    return result;
}

DeviceSearchResult compositor_input_device_find_by_type(CompositorInputDeviceType type) {
    DeviceSearchResult result = {0};
    
    CompositorInputDevice* devices = NULL;
    int count = 0;
    
    if (compositor_input_manager_get_devices(&devices, &count) == COMPOSITOR_OK) {
        for (int i = 0; i < count; i++) {
            if (devices[i].type == type) {
                result.device = &devices[i];
                result.index = i;
                result.found = true;
                break;
            }
        }
    }
    
    return result;
}

CompositorInputDevice* compositor_input_device_find_highest_priority(void) {
    return compositor_input_manager_get_highest_priority_active_device();
}

// 设备迭代函数
bool compositor_input_device_for_each(DeviceIteratorCallback callback, void* user_data) {
    if (!callback) {
        return false;
    }
    
    CompositorInputDevice* devices = NULL;
    int count = 0;
    
    if (compositor_input_manager_get_devices(&devices, &count) != COMPOSITOR_OK) {
        return false;
    }
    
    for (int i = 0; i < count; i++) {
        if (!callback(&devices[i], user_data)) {
            return false;
        }
    }
    
    return true;
}

// 设备迭代辅助结构体
typedef struct {
    CompositorInputDeviceType type;
    int count;
} DeviceCountByTypeData;

typedef struct {
    int count;
} DeviceCountEnabledData;

// 设备迭代回调函数
static bool count_device_by_type_callback(CompositorInputDevice* device, void* user_data) {
    DeviceCountByTypeData* data = (DeviceCountByTypeData*)user_data;
    if (device->type == data->type) {
        data->count++;
    }
    return true;
}

static bool count_enabled_device_callback(CompositorInputDevice* device, void* user_data) {
    DeviceCountEnabledData* data = (DeviceCountEnabledData*)user_data;
    if (device->enabled) {
        data->count++;
    }
    return true;
}

int compositor_input_device_count_by_type(CompositorInputDeviceType type) {
    DeviceCountByTypeData data = {type, 0};
    compositor_input_device_for_each(count_device_by_type_callback, &data);
    return data.count;
}

int compositor_input_device_count_enabled(void) {
    DeviceCountEnabledData data = {0};
    compositor_input_device_for_each(count_enabled_device_callback, &data);
    return data.count;
}

// 设备优先级管理
void compositor_input_device_set_default_priorities(void) {
    for (size_t i = 0; i < sizeof(default_priority_map) / sizeof(default_priority_map[0]); i++) {
        compositor_input_manager_set_device_priority(
            default_priority_map[i].type, 
            default_priority_map[i].priority
        );
    }
}

int compositor_input_device_get_default_priority(CompositorInputDeviceType type) {
    for (size_t i = 0; i < sizeof(default_priority_map) / sizeof(default_priority_map[0]); i++) {
        if (default_priority_map[i].type == type) {
            return default_priority_map[i].priority;
        }
    }
    
    return DEVICE_PRIORITY_LOW;
}

void compositor_input_device_update_priorities_by_type(CompositorInputDeviceType type, int priority) {
    compositor_input_manager_set_device_priority(type, priority);
}

// 设备状态检查
bool compositor_input_device_is_active(const CompositorInputDevice* device) {
    if (!device) {
        return false;
    }
    
    CompositorInputDevice* active_device = compositor_input_manager_get_active_device();
    return active_device == device;
}

bool compositor_input_device_is_enabled(const CompositorInputDevice* device) {
    return device ? device->enabled : false;
}

bool compositor_input_device_has_capability(const CompositorInputDevice* device, DeviceCapability capability) {
    if (!device) {
        return false;
    }
    
    return (device->capabilities & capability) != 0;
}

// 设备配置管理
void compositor_input_device_init_config(DeviceConfig* config) {
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(DeviceConfig));
    config->max_simultaneous_touches = 10;
    config->adaptive_input = true;
    config->input_response_time = 5;
    
    // 设置默认优先级
    for (size_t i = 0; i < sizeof(default_priority_map) / sizeof(default_priority_map[0]); i++) {
        if (default_priority_map[i].type < 10) {
            config->device_type_supported[default_priority_map[i].type] = true;
            config->device_priority[default_priority_map[i].type] = default_priority_map[i].priority;
        }
    }
}

void compositor_input_device_copy_config(const DeviceConfig* src, DeviceConfig* dst) {
    if (!src || !dst) {
        return;
    }
    
    *dst = *src;
}

bool compositor_input_device_is_type_supported(const DeviceConfig* config, CompositorInputDeviceType type) {
    if (!config || type < 0 || type >= 10) {
        return false;
    }
    
    return config->device_type_supported[type];
}

int compositor_input_device_get_type_priority(const DeviceConfig* config, CompositorInputDeviceType type) {
    if (!config || type < 0 || type >= 10) {
        return DEVICE_PRIORITY_LOW;
    }
    
    return config->device_priority[type];
}

void compositor_input_device_set_type_priority(DeviceConfig* config, CompositorInputDeviceType type, int priority) {
    if (!config || type < 0 || type >= 10 || priority < 0 || priority > 10) {
        return;
    }
    
    config->device_priority[type] = priority;
}

// 设备能力管理
uint32_t compositor_input_device_get_default_capabilities(CompositorInputDeviceType type) {
    for (size_t i = 0; i < sizeof(default_capabilities_map) / sizeof(default_capabilities_map[0]); i++) {
        if (default_capabilities_map[i].type == type) {
            return default_capabilities_map[i].capabilities;
        }
    }
    
    return DEVICE_CAPABILITY_NONE;
}

bool compositor_input_device_supports_capability(CompositorInputDeviceType type, DeviceCapability capability) {
    uint32_t capabilities = compositor_input_device_get_default_capabilities(type);
    return (capabilities & capability) != 0;
}

void compositor_input_device_add_capability(CompositorInputDevice* device, DeviceCapability capability) {
    if (!device) {
        return;
    }
    
    device->capabilities |= capability;
}

void compositor_input_device_remove_capability(CompositorInputDevice* device, DeviceCapability capability) {
    if (!device) {
        return;
    }
    
    device->capabilities &= ~capability;
}

// 设备创建和销毁辅助函数
CompositorInputDevice* compositor_input_device_create(CompositorInputDeviceType type, const char* name, int device_id) {
    CompositorInputDevice* device = (CompositorInputDevice*)malloc(sizeof(CompositorInputDevice));
    if (!device) {
        return NULL;
    }
    
    memset(device, 0, sizeof(CompositorInputDevice));
    device->type = type;
    device->device_id = device_id;
    device->name = name ? strdup(name) : strdup("Unknown Device");
    device->enabled = true;
    device->priority = compositor_input_device_get_default_priority(type);
    device->capabilities = compositor_input_device_get_default_capabilities(type);
    
    return device;
}

void compositor_input_device_destroy(CompositorInputDevice* device) {
    if (!device) {
        return;
    }
    
    if (device->name) {
        free((void*)device->name);
    }
    
    free(device);
}

void compositor_input_device_copy(const CompositorInputDevice* src, CompositorInputDevice* dst) {
    if (!src || !dst) {
        return;
    }
    
    // 释放目标设备的名称
    if (dst->name) {
        free((void*)dst->name);
    }
    
    // 复制源设备
    *dst = *src;
    
    // 复制名称
    if (src->name) {
        dst->name = strdup(src->name);
    }
}

// 设备验证函数
bool compositor_input_device_is_valid(const CompositorInputDevice* device) {
    if (!device) {
        return false;
    }
    
    return compositor_input_device_is_valid_type(device->type) && 
           compositor_input_device_is_valid_id(device->device_id) &&
           device->name != NULL;
}

bool compositor_input_device_is_valid_type(CompositorInputDeviceType type) {
    return type >= COMPOSITOR_INPUT_DEVICE_TYPE_UNKNOWN && 
           type <= COMPOSITOR_INPUT_DEVICE_TYPE_GAMEPAD;
}

bool compositor_input_device_is_valid_id(int device_id) {
    return device_id >= 0;
}

// 设备调试和日志
void compositor_input_device_log_info(const CompositorInputDevice* device) {
    if (!device) {
        log_message(COMPOSITOR_LOG_INFO, "Device: NULL");
        return;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Device: ID=%d, Type=%d, Name=%s, Enabled=%s, Priority=%d, Capabilities=0x%x",
               device->device_id, device->type, device->name ? device->name : "NULL",
               device->enabled ? "true" : "false", device->priority, device->capabilities);
}

void compositor_input_device_log_error(const char* message, const CompositorInputDevice* device) {
    if (!device) {
        log_message(COMPOSITOR_LOG_ERROR, "%s (Device: NULL)", message ? message : "Error");
        return;
    }
    
    log_message(COMPOSITOR_LOG_ERROR, "%s (Device: ID=%d, Type=%d, Name=%s)",
               message ? message : "Error", device->device_id, device->type,
               device->name ? device->name : "NULL");
}

void compositor_input_device_log_debug(const char* message, const CompositorInputDevice* device) {
    if (!device) {
        log_message(COMPOSITOR_LOG_DEBUG, "%s (Device: NULL)", message ? message : "Debug");
        return;
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "%s (Device: ID=%d, Type=%d, Name=%s)",
               message ? message : "Debug", device->device_id, device->type,
               device->name ? device->name : "NULL");
}