/*
 * WinDroids Compositor
 * Input Device Management Implementation
 */

#include "compositor_input_device.h"
#include "compositor_utils.h"
#include "compositor_input_type.h"
#include <string.h>
#include <stdlib.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 输入设备列表
static CompositorInputDevice* g_input_devices = NULL;
static int g_input_device_count = 0;
static int g_input_device_capacity = 0;

// 活动输入设备
static CompositorInputDevice* g_active_device = NULL;

// 设备特定配置
static struct {
    bool device_type_supported[10]; // 支持的设备类型
    int max_simultaneous_touches;   // 最大同时触摸点数
    bool pressure_sensitivity;      // 压力感应
    bool tilt_support;              // 倾斜支持
    bool rotation_support;          // 旋转支持
} g_input_device_config = {
    .device_type_supported = {false}, // 初始化为全不支持
    .max_simultaneous_touches = 10,
    .pressure_sensitivity = false,
    .tilt_support = false,
    .rotation_support = false
};

// 设置合成器状态引用
void compositor_input_device_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 安全内存重新分配函数
static void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    return new_ptr;
}

// 初始化设备管理系统
int compositor_input_device_init(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 初始化设备列表
    g_input_devices = NULL;
    g_input_device_count = 0;
    g_input_device_capacity = 0;
    g_active_device = NULL;
    
    // 初始化设备配置
    memset(&g_input_device_config, 0, sizeof(g_input_device_config));
    g_input_device_config.max_simultaneous_touches = 10;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Input device management system initialized");
    return COMPOSITOR_OK;
}

// 清理设备管理系统
void compositor_input_device_cleanup(void) {
    // 清理所有设备
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].name) {
            free(g_input_devices[i].name);
        }
        if (g_input_devices[i].device_data) {
            free(g_input_devices[i].device_data);
        }
    }
    
    // 释放设备列表
    if (g_input_devices) {
        free(g_input_devices);
        g_input_devices = NULL;
    }
    
    g_input_device_count = 0;
    g_input_device_capacity = 0;
    g_active_device = NULL;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Input device management system cleaned up");
}

// 创建输入设备
static CompositorInputDevice* create_input_device(CompositorInputDeviceType type, const char* name, int device_id) {
    CompositorInputDevice* device = (CompositorInputDevice*)safe_malloc(sizeof(CompositorInputDevice));
    if (!device) {
        return NULL;
    }
    
    memset(device, 0, sizeof(CompositorInputDevice));
    device->type = type;
    device->device_id = device_id;
    device->name = strdup(name ? name : "Unknown Device");
    device->enabled = true;
    device->device_data = NULL;
    
    // 初始化游戏控制器按钮状态
    memset(&device->gamepad_buttons, 0, sizeof(device->gamepad_buttons));
    
    // 根据设备类型初始化特定能力标志
    device->has_pressure_sensor = false;
    device->has_tilt_sensor = false;
    device->has_rotation_sensor = false;
    device->has_accelerometer = false;
    
    // 根据设备类型设置默认能力
    switch (type) {
        case COMPOSITOR_INPUT_DEVICE_TYPE_PEN:
            // 触控笔默认支持压力和倾斜传感器
            device->has_pressure_sensor = true;
            device->has_tilt_sensor = true;
            device->has_rotation_sensor = true;
            break;
            
        case COMPOSITOR_INPUT_DEVICE_TYPE_TOUCHSCREEN:
            // 触摸屏可能支持压力感应
            device->has_pressure_sensor = true;
            break;
            
        case COMPOSITOR_INPUT_DEVICE_TYPE_GAMEPAD:
            // 游戏手柄可能包含加速度计
            device->has_accelerometer = true;
            break;
            
        default:
            // 其他设备类型默认无特殊传感器
            break;
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Created input device: id=%d, type=%d, name=%s, pressure=%d, tilt=%d",
               device_id, type, name ? name : "Unknown Device", device->has_pressure_sensor, device->has_tilt_sensor);
    
    return device;
}

// 注册输入设备
int compositor_input_register_device(CompositorInputDeviceType type, const char* name, int device_id) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (!name || device_id < 0) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 检查设备是否已注册
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            log_message(COMPOSITOR_LOG_WARN, "Device already registered: %d", device_id);
            return COMPOSITOR_ERROR_ALREADY_EXISTS;
        }
    }
    
    // 检查是否需要扩容
    if (g_input_device_count >= g_input_device_capacity) {
        int new_capacity = g_input_device_capacity == 0 ? 8 : g_input_device_capacity * 2;
        CompositorInputDevice* new_devices = (CompositorInputDevice*)safe_realloc(
            g_input_devices, sizeof(CompositorInputDevice) * new_capacity);
        if (!new_devices) {
            return COMPOSITOR_ERROR_OUT_OF_MEMORY;
        }
        g_input_devices = new_devices;
        g_input_device_capacity = new_capacity;
    }
    
    // 创建并添加设备
    CompositorInputDevice* device = create_input_device(type, name, device_id);
    if (!device) {
        return COMPOSITOR_ERROR_OUT_OF_MEMORY;
    }
    
    g_input_devices[g_input_device_count++] = *device;
    free(device);  // 数据已经复制到数组中
    
    // 如果是第一个设备，设置为活动设备
    if (g_input_device_count == 1) {
        g_active_device = &g_input_devices[0];
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Registered input device: %s (ID: %d, Type: %d)", 
               name, device_id, type);
    
    return COMPOSITOR_OK;
}

// 注销输入设备
int compositor_input_unregister_device(int device_id) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 查找设备
    int index = -1;
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        log_message(COMPOSITOR_LOG_WARN, "Device not found: %d", device_id);
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    // 释放设备资源
    if (g_input_devices[index].name) {
        free(g_input_devices[index].name);
        g_input_devices[index].name = NULL;
    }
    
    if (g_input_devices[index].device_data) {
        free(g_input_devices[index].device_data);
        g_input_devices[index].device_data = NULL;
    }
    
    // 如果是活动设备，清除活动设备引用
    if (g_active_device == &g_input_devices[index]) {
        g_active_device = NULL;
    }
    
    // 移动数组元素以保持顺序
    for (int i = index; i < g_input_device_count - 1; i++) {
        g_input_devices[i] = g_input_devices[i + 1];
        // 如果移动的是活动设备，更新活动设备指针
        if (g_active_device == &g_input_devices[i + 1]) {
            g_active_device = &g_input_devices[i];
        }
    }
    
    g_input_device_count--;
    
    // 如果没有设备了，释放内存
    if (g_input_device_count == 0) {
        free(g_input_devices);
        g_input_devices = NULL;
        g_input_device_capacity = 0;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Unregistered input device: %d", device_id);
    
    return COMPOSITOR_OK;
}

// 启用/禁用输入设备
int compositor_input_enable_device(int device_id, bool enabled) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (device_id < 0) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            g_input_devices[i].enabled = enabled;
            log_message(COMPOSITOR_LOG_INFO, "Device %d (%s) %s", 
                       device_id, g_input_devices[i].name, enabled ? "enabled" : "disabled");
            return COMPOSITOR_OK;
        }
    }
    
    log_message(COMPOSITOR_LOG_WARN, "Device not found: %d", device_id);
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 获取设备状态
int compositor_input_device_get_status(int device_id, bool* enabled) {
    if (device_id < 0 || !enabled) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找设备
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            *enabled = g_input_devices[i].enabled;
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 设置设备优先级
int compositor_input_device_set_priority(int device_id, int priority) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (device_id < 0 || priority < 0 || priority > 9) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找设备
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            g_input_devices[i].priority = priority;
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 获取设备优先级
int compositor_input_device_get_priority(int device_id, int* priority) {
    if (device_id < 0 || !priority) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找设备
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            *priority = g_input_devices[i].priority;
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 设置活动设备
int compositor_input_device_set_active(int device_id) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (device_id < 0) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找设备
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            g_active_device = &g_input_devices[i];
            log_message(COMPOSITOR_LOG_DEBUG, "Active device set to: %s (ID: %d)",
                       g_input_devices[i].name, device_id);
            return COMPOSITOR_OK;
        }
    }
    
    log_message(COMPOSITOR_LOG_WARN, "Device not found, cannot set as active: %d", device_id);
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 获取活动设备
int compositor_input_device_get_active(CompositorInputDevice* device) {
    if (!device) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    if (!g_active_device) {
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    *device = *g_active_device;
    return COMPOSITOR_OK;
}

// 获取设备信息
int compositor_input_device_get_info(int device_id, CompositorInputDevice* info) {
    if (device_id < 0 || !info) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找设备
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            *info = g_input_devices[i];
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 获取所有设备
int compositor_input_device_get_all(CompositorInputDevice** devices, int* count, int max_count) {
    if (!devices || !count || max_count <= 0) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 限制返回的设备数量不超过max_count
    int return_count = (g_input_device_count < max_count) ? g_input_device_count : max_count;
    
    // 分配内存
    *devices = malloc(return_count * sizeof(CompositorInputDevice));
    if (!*devices) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for device list");
        return COMPOSITOR_ERROR_OUT_OF_MEMORY;
    }
    
    // 复制设备信息
    memcpy(*devices, g_input_devices, return_count * sizeof(CompositorInputDevice));
    *count = return_count;
    
    return COMPOSITOR_OK;
}

// 获取指定类型的设备
int compositor_input_device_get_by_type(CompositorInputDeviceType type, CompositorInputDevice** devices, int* count, int max_count) {
    if (!devices || !count || max_count <= 0) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 计算匹配的设备数量
    int match_count = 0;
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].type == type) {
            match_count++;
        }
    }
    
    // 限制返回的设备数量不超过max_count
    int return_count = (match_count < max_count) ? match_count : max_count;
    
    // 分配结果数组
    *devices = malloc(return_count * sizeof(CompositorInputDevice));
    if (!*devices) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for device list");
        return COMPOSITOR_ERROR_OUT_OF_MEMORY;
    }
    
    // 填充结果数组
    int index = 0;
    for (int i = 0; i < g_input_device_count && index < return_count; i++) {
        if (g_input_devices[i].type == type) {
            (*devices)[index] = g_input_devices[i];
            index++;
        }
    }
    
    *count = return_count;
    
    return COMPOSITOR_OK;
}

// 获取设备状态
int compositor_input_get_device(int device_id, CompositorInputDevice* device) {
    if (!device || device_id < 0) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            *device = g_input_devices[i];
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 获取设备数量
int compositor_input_get_device_count(void) {
    return g_input_device_count;
}

// 获取设备列表
int compositor_input_get_devices(CompositorInputDevice** devices, int* count, int max_count) {
    if (!devices || !count || max_count <= 0) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 限制返回的设备数量不超过max_count
    int return_count = (g_input_device_count < max_count) ? g_input_device_count : max_count;
    
    // 分配内存
    *devices = malloc(return_count * sizeof(CompositorInputDevice));
    if (!*devices) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for device list");
        return COMPOSITOR_ERROR_OUT_OF_MEMORY;
    }
    
    // 复制设备信息
    memcpy(*devices, g_input_devices, return_count * sizeof(CompositorInputDevice));
    *count = return_count;
    
    return COMPOSITOR_OK;
}

// 设置设备特定配置
int compositor_input_set_device_config(int device_id, void* config) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            // 释放旧配置
            if (g_input_devices[i].device_data) {
                free(g_input_devices[i].device_data);
            }
            
            // 设置新配置
            g_input_devices[i].device_data = config;
            log_message(COMPOSITOR_LOG_DEBUG, "Set device config for ID: %d", device_id);
            return COMPOSITOR_OK;
        }
    }
    
    log_message(COMPOSITOR_LOG_WARN, "Device not found: %d", device_id);
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 获取设备特定配置
int compositor_input_get_device_config(int device_id, void** config) {
    if (!config || device_id < 0) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            *config = g_input_devices[i].device_data;
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_NOT_FOUND;
}

// 获取活动输入设备
int compositor_input_get_active_device(CompositorInputDevice* device) {
    if (!device) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    if (!g_active_device) {
        return COMPOSITOR_ERROR_NOT_FOUND;
    }
    
    *device = *g_active_device;
    return COMPOSITOR_OK;
}