#include "compositor_input_manager.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 输入设备列表
static CompositorInputDevice* g_input_devices = NULL;
static int g_input_device_count = 0;
static int g_input_device_capacity = 0;

// 输入捕获模式
static CompositorInputCaptureMode g_input_capture_mode = COMPOSITOR_INPUT_CAPTURE_MODE_NORMAL;

// 活动输入设备
static CompositorInputDevice* g_active_device = NULL;

// 设备特定配置
static struct {
    bool device_type_supported[10]; // 支持的设备类型
    int max_simultaneous_touches;   // 最大同时触摸点数
    int device_priority[10];        // 设备优先级（数值越高优先级越高）
    bool adaptive_input;            // 自适应输入处理
    int input_response_time;        // 输入响应时间（毫秒）
} g_input_device_config = {
    .device_type_supported = {false}, // 初始化为全不支持
    .max_simultaneous_touches = 10,
    .device_priority = {0},        // 初始化优先级为0
    .adaptive_input = true,         // 默认启用自适应输入
    .input_response_time = 5        // 默认5毫秒响应时间
};

// 安全内存分配
static void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory of size %zu", size);
    }
    return ptr;
}

// 安全内存重分配
static void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to reallocate memory of size %zu", size);
    }
    return new_ptr;
}

// 初始化输入设备管理模块
int compositor_input_manager_init(void) {
    g_input_devices = NULL;
    g_input_device_count = 0;
    g_input_device_capacity = 0;
    g_active_device = NULL;
    g_input_capture_mode = COMPOSITOR_INPUT_CAPTURE_MODE_NORMAL;
    
    // 重置设备配置
    memset(&g_input_device_config, 0, sizeof(g_input_device_config));
    g_input_device_config.max_simultaneous_touches = 10;
    g_input_device_config.adaptive_input = true;
    g_input_device_config.input_response_time = 5;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Input manager module initialized");
    return 0;
}

// 清理输入设备管理模块
void compositor_input_manager_cleanup(void) {
    // 释放所有设备
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].name) {
            free((void*)g_input_devices[i].name);
        }
    }
    
    if (g_input_devices) {
        free(g_input_devices);
        g_input_devices = NULL;
    }
    
    g_input_device_count = 0;
    g_input_device_capacity = 0;
    g_active_device = NULL;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Input manager module cleaned up");
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
    memset(device->gamepad_buttons, 0, sizeof(device->gamepad_buttons));
    
    // 设备特定能力初始化已移至compositor_input_gamepad.c
    
    log_message(COMPOSITOR_LOG_DEBUG, "Created input device: id=%d, type=%d, name=%s",
               device_id, type, name ? name : "Unknown Device");
    
    return device;
}

// 注册输入设备
int compositor_input_manager_register_device(CompositorInputDeviceType type, const char* name, int device_id) {
    // 检查设备是否已注册
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            log_message(COMPOSITOR_LOG_WARN, "Device with ID %d already registered", device_id);
            return COMPOSITOR_ERROR_ALREADY_EXISTS;
        }
    }
    
    // 扩展设备数组（如果需要）
    if (g_input_device_count >= g_input_device_capacity) {
        int new_capacity = g_input_device_capacity == 0 ? 16 : g_input_device_capacity * 2;
        CompositorInputDevice* new_devices = (CompositorInputDevice*)safe_realloc(
            g_input_devices, sizeof(CompositorInputDevice) * new_capacity);
        if (!new_devices) {
            return COMPOSITOR_ERROR_MEMORY;
        }
        g_input_devices = new_devices;
        g_input_device_capacity = new_capacity;
    }
    
    // 创建新设备
    CompositorInputDevice* device = create_input_device(type, name, device_id);
    if (!device) {
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    // 设置设备优先级（根据设备类型）
    switch (type) {
        case COMPOSITOR_DEVICE_TYPE_MOUSE:
            device->priority = 8; // 鼠标优先级较高
            break;
        case COMPOSITOR_DEVICE_TYPE_KEYBOARD:
            device->priority = 9; // 键盘优先级最高
            break;
        case COMPOSITOR_DEVICE_TYPE_TOUCHSCREEN:
            device->priority = 7; // 触摸屏优先级中等
            break;
        case COMPOSITOR_DEVICE_TYPE_PEN:
            device->priority = 6; // 触控笔优先级中等
            break;
        case COMPOSITOR_DEVICE_TYPE_GAMEPAD:
            device->priority = 5; // 游戏手柄优先级较低
            break;
        default:
            device->priority = 3; // 其他设备优先级最低
            break;
    }
    
    // 添加到设备列表
    g_input_devices[g_input_device_count] = *device;
    g_input_device_count++;
    
    // 更新全局设备配置
    g_input_device_config.device_type_supported[type] = true;
    g_input_device_config.device_priority[type] = device->priority;
    
    // 如果没有活动设备，设置此设备为活动设备
    if (!g_active_device) {
        g_active_device = &g_input_devices[g_input_device_count - 1];
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Registered input device: %s (ID: %d, Type: %d, Priority: %d)", 
               name, device_id, type, device->priority);
    
    return COMPOSITOR_OK;
}

// 注销输入设备
int compositor_input_manager_unregister_device(int device_id) {
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            // 释放设备名称
            if (g_input_devices[i].name) {
                free((void*)g_input_devices[i].name);
            }
            
            // 如果是活动设备，清除活动设备引用
            if (g_active_device == &g_input_devices[i]) {
                g_active_device = NULL;
            }
            
            // 移除设备（将最后一个设备移到当前位置）
            if (i < g_input_device_count - 1) {
                g_input_devices[i] = g_input_devices[g_input_device_count - 1];
            }
            g_input_device_count--;
            
            log_message(COMPOSITOR_LOG_INFO, "Unregistered input device: ID %d", device_id);
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
}

// 获取输入设备
CompositorInputDevice* compositor_input_manager_get_device(int device_id) {
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            return &g_input_devices[i];
        }
    }
    return NULL;
}

// 获取输入设备列表
int compositor_input_manager_get_devices(CompositorInputDevice** out_devices, int* out_count) {
    if (!out_devices || !out_count) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    *out_devices = g_input_devices;
    *out_count = g_input_device_count;
    
    return COMPOSITOR_OK;
}

// 设置设备启用状态
int compositor_input_manager_set_device_enabled(int device_id, bool enabled) {
    CompositorInputDevice* device = compositor_input_manager_get_device(device_id);
    if (!device) {
        return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
    }
    
    device->enabled = enabled;
    log_message(COMPOSITOR_LOG_DEBUG, "Device %d enabled: %s", device_id, enabled ? "true" : "false");
    
    return COMPOSITOR_OK;
}

// 设置输入设备优先级
int compositor_input_manager_set_device_priority(CompositorInputDeviceType type, int priority) {
    if (type < 0 || type >= 10 || priority < 0 || priority > 10) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    g_input_device_config.device_priority[type] = priority;
    
    // 更新已注册设备的优先级
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].type == type) {
            g_input_devices[i].priority = priority;
        }
    }
    
    // 重新选择活动设备
    g_active_device = compositor_input_manager_get_highest_priority_active_device();
    
    log_message(COMPOSITOR_LOG_INFO, "Set device type %d priority to %d", type, priority);
    
    return COMPOSITOR_OK;
}

// 获取最高优先级的活动设备
CompositorInputDevice* compositor_input_manager_get_highest_priority_active_device(void) {
    CompositorInputDevice* best_device = NULL;
    int highest_priority = -1;
    
    for (int i = 0; i < g_input_device_count; i++) {
        CompositorInputDevice* device = &g_input_devices[i];
        if (device->enabled && g_input_device_config.device_priority[device->type] > highest_priority) {
            highest_priority = g_input_device_config.device_priority[device->type];
            best_device = device;
        }
    }
    
    return best_device;
}

// 设置活动输入设备
void compositor_input_manager_set_active_device(int device_id) {
    g_active_device = compositor_input_manager_get_device(device_id);
}

// 获取活动输入设备
CompositorInputDevice* compositor_input_manager_get_active_device(void) {
    return g_active_device;
}

// 启用/禁用自适应输入处理
int compositor_input_manager_set_adaptive_mode(bool enabled) {
    g_input_device_config.adaptive_input = enabled;
    log_message(COMPOSITOR_LOG_INFO, "Adaptive input processing %s", enabled ? "enabled" : "disabled");
    return COMPOSITOR_OK;
}

// 获取输入捕获模式
CompositorInputCaptureMode compositor_input_manager_get_capture_mode(void) {
    return g_input_capture_mode;
}

// 设置输入捕获模式
void compositor_input_manager_set_capture_mode(CompositorInputCaptureMode mode) {
    g_input_capture_mode = mode;
    log_message(COMPOSITOR_LOG_DEBUG, "Input capture mode set to: %d", mode);
}

// 检查设备类型是否支持
bool compositor_input_manager_is_device_type_supported(CompositorDeviceType device_type) {
    if (device_type < 0 || device_type >= 10) {
        return false;
    }
    return g_input_device_config.device_type_supported[device_type];
}