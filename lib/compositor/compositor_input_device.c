/*
 * WinDroids Compositor
 * Input Device Management Implementation
 */

#include "compositor_input_device.h"
#include "compositor_utils.h"
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
        case COMPOSITOR_DEVICE_TYPE_PEN:
            // 触控笔默认支持压力和倾斜传感器
            device->has_pressure_sensor = true;
            device->has_tilt_sensor = true;
            device->has_rotation_sensor = true;
            break;
            
        case COMPOSITOR_DEVICE_TYPE_TOUCHSCREEN:
            // 触摸屏可能支持压力感应
            device->has_pressure_sensor = true;
            break;
            
        case COMPOSITOR_DEVICE_TYPE_GAMEPAD:
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
    
    // 检查设备是否已注册
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            log_message(COMPOSITOR_LOG_WARN, "Device already registered: %d", device_id);
            return COMPOSITOR_ERROR_DEVICE_EXISTS;
        }
    }
    
    // 检查是否需要扩容
    if (g_input_device_count >= g_input_device_capacity) {
        int new_capacity = g_input_device_capacity == 0 ? 8 : g_input_device_capacity * 2;
        CompositorInputDevice* new_devices = (CompositorInputDevice*)safe_realloc(
            g_input_devices, sizeof(CompositorInputDevice) * new_capacity);
        if (!new_devices) {
            return COMPOSITOR_ERROR_MEMORY;
        }
        g_input_devices = new_devices;
        g_input_device_capacity = new_capacity;
    }
    
    // 创建并添加设备
    CompositorInputDevice* device = create_input_device(type, name, device_id);
    if (!device) {
        return COMPOSITOR_ERROR_MEMORY;
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
    
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            // 释放设备资源
            if (g_input_devices[i].name) {
                free(g_input_devices[i].name);
            }
            if (g_input_devices[i].device_data) {
                free(g_input_devices[i].device_data);
            }
            
            // 如果是活动设备，清除活动设备引用
            if (g_active_device == &g_input_devices[i]) {
                g_active_device = NULL;
            }
            
            // 将最后一个设备移到当前位置，减少计数
            if (i < g_input_device_count - 1) {
                g_input_devices[i] = g_input_devices[g_input_device_count - 1];
                // 如果移动的是活动设备，更新活动设备指针
                if (g_active_device == &g_input_devices[g_input_device_count - 1]) {
                    g_active_device = &g_input_devices[i];
                }
            }
            
            g_input_device_count--;
            
            log_message(COMPOSITOR_LOG_INFO, "Unregistered input device: %d", device_id);
            return COMPOSITOR_OK;
        }
    }
    
    log_message(COMPOSITOR_LOG_WARN, "Device not found: %d", device_id);
    return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
}

// 启用/禁用输入设备
int compositor_input_enable_device(int device_id, bool enabled) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
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
    return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
}

// 获取设备状态
CompositorInputDevice* compositor_input_get_device(int device_id) {
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            return &g_input_devices[i];
        }
    }
    return NULL;
}

// 获取设备数量
int compositor_input_get_device_count(void) {
    return g_input_device_count;
}

// 获取设备列表
CompositorInputDevice* compositor_input_get_devices(void) {
    return g_input_devices;
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
    return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
}

// 获取设备特定配置
void* compositor_input_get_device_config(int device_id) {
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            return g_input_devices[i].device_data;
        }
    }
    return NULL;
}

// 获取活动输入设备
CompositorInputDevice* compositor_input_get_active_device(void) {
    return g_active_device;
}

// 设置活动输入设备
void compositor_input_set_active_device(int device_id) {
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            g_active_device = &g_input_devices[i];
            log_message(COMPOSITOR_LOG_DEBUG, "Active device set to: %s (ID: %d)",
                       g_input_devices[i].name, device_id);
            return;
        }
    }
    
    log_message(COMPOSITOR_LOG_WARN, "Device not found, cannot set as active: %d", device_id);
}