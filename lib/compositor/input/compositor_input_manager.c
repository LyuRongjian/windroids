#include "compositor_input_manager.h"
#include "compositor_input_device_utils.h"
#include "compositor_input_window_switch.h"
#include "compositor_window_preview.h"
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
    // 使用模块工具初始化
    if (!compositor_validate_params("compositor_input_manager_init", 0, NULL)) {
        return COMPOSITOR_ERROR_INVALID_PARAM;
    }
    
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
    
    compositor_log_debug("Input manager module initialized");
    return COMPOSITOR_OK;
}

// 清理输入设备管理模块
void compositor_input_manager_cleanup(void) {
    // 使用模块工具清理
    if (!compositor_validate_params("compositor_input_manager_cleanup", 0, NULL)) {
        return;
    }
    
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
    
    compositor_log_debug("Input manager module cleaned up");
}

// 创建输入设备
static CompositorInputDevice* create_input_device(CompositorInputDeviceType type, const char* name, int device_id) {
    // 使用设备工具模块创建设备
    CompositorInputDevice* device = compositor_input_device_create(type, name, device_id);
    if (!device) {
        return NULL;
    }
    
    // 初始化游戏控制器按钮状态
    memset(device->gamepad_buttons, 0, sizeof(device->gamepad_buttons));
    
    // 记录日志
    compositor_input_device_log_debug("Created input device", device);
    
    return device;
}

// 注册输入设备
int compositor_input_manager_register_device(CompositorInputDeviceType type, const char* name, int device_id) {
    // 检查设备是否已注册
    DeviceSearchResult search_result = compositor_input_device_find_by_id(device_id);
    if (search_result.found) {
        log_message(COMPOSITOR_LOG_WARN, "Device with ID %d already registered", device_id);
        return COMPOSITOR_ERROR_ALREADY_EXISTS;
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
    
    // 使用设备工具模块设置默认优先级
    device->priority = compositor_input_device_get_default_priority(type);
    
    // 添加到设备列表
    g_input_devices[g_input_device_count] = *device;
    g_input_device_count++;
    
    // 释放临时创建的设备，避免内存泄漏
    compositor_input_device_destroy(device);
    
    // 更新全局设备配置
    g_input_device_config.device_type_supported[type] = true;
    g_input_device_config.device_priority[type] = g_input_devices[g_input_device_count - 1].priority;
    
    // 如果没有活动设备，设置此设备为活动设备
    if (!g_active_device) {
        g_active_device = &g_input_devices[g_input_device_count - 1];
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Registered input device: %s (ID: %d, Type: %d, Priority: %d)", 
               name, device_id, type, g_input_devices[g_input_device_count - 1].priority);
    
    return COMPOSITOR_OK;
}

// 注销输入设备
int compositor_input_manager_unregister_device(int device_id) {
    DeviceSearchResult search_result = compositor_input_device_find_by_id(device_id);
    if (!search_result.found) {
        return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
    }
    
    CompositorInputDevice* device = search_result.device;
    int index = search_result.index;
    
    // 释放设备名称
    if (device->name) {
        free((void*)device->name);
    }
    
    // 如果是活动设备，清除活动设备引用
    if (g_active_device == device) {
        g_active_device = NULL;
    }
    
    // 移除设备（将最后一个设备移到当前位置）
    if (index < g_input_device_count - 1) {
        g_input_devices[index] = g_input_devices[g_input_device_count - 1];
    }
    g_input_device_count--;
    
    log_message(COMPOSITOR_LOG_INFO, "Unregistered input device: ID %d", device_id);
    return COMPOSITOR_OK;
}

// 获取输入设备
CompositorInputDevice* compositor_input_manager_get_device(int device_id) {
    DeviceSearchResult search_result = compositor_input_device_find_by_id(device_id);
    return search_result.found ? search_result.device : NULL;
}

// 获取输入设备列表
// 获取输入设备列表
int compositor_input_manager_get_devices(CompositorInputDevice** out_devices, int* out_count) {
    // 使用模块工具验证参数
    if (!compositor_validate_params("compositor_input_manager_get_devices", 
                                   2, (void*[]){out_devices, out_count})) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    *out_devices = g_input_devices;
    *out_count = g_input_device_count;
    
    return COMPOSITOR_OK;
}

// 设置设备启用状态
// 设置设备启用状态
int compositor_input_manager_set_device_enabled(int device_id, bool enabled) {
    DeviceSearchResult search_result = compositor_input_device_find_by_id(device_id);
    if (!search_result.found) {
        return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
    }
    
    CompositorInputDevice* device = search_result.device;
    device->enabled = enabled;
    
    compositor_input_device_log_debug("Device enabled status changed", device);
    
    return COMPOSITOR_OK;
}

// 设置输入设备优先级
// 设置输入设备优先级
int compositor_input_manager_set_device_priority(CompositorInputDeviceType type, int priority) {
    // 使用设备工具模块验证参数
    if (!compositor_validate_device_priority(type, priority)) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 使用设备工具模块更新所有同类型设备的优先级
    compositor_input_device_set_priority_for_type(g_input_devices, g_input_device_count, type, priority);
    
    // 更新全局配置
    g_input_device_config.device_priority[type] = priority;
    
    // 重新选择活动设备
    g_active_device = compositor_input_manager_get_highest_priority_active_device();
    
    compositor_log_info("Set device type %d priority to %d", type, priority);
    
    return COMPOSITOR_OK;
}

// 获取最高优先级的活动设备
CompositorInputDevice* compositor_input_manager_get_highest_priority_active_device(void) {
    return compositor_input_device_find_highest_priority();
}

// 设置活动输入设备
// 设置活动输入设备
void compositor_input_manager_set_active_device(int device_id) {
    DeviceSearchResult search_result = compositor_input_device_find_by_id(device_id);
    if (search_result.found) {
        g_active_device = search_result.device;
        compositor_input_device_log_debug("Set as active device", g_active_device);
    } else {
        log_message(COMPOSITOR_LOG_WARNING, "Failed to set active device: ID %d not found", device_id);
        g_active_device = NULL;
    }
}

// 获取活动输入设备
CompositorInputDevice* compositor_input_manager_get_active_device(void) {
    return g_active_device;
}

// 启用/禁用自适应输入处理
int compositor_input_manager_set_adaptive_mode(bool enabled) {
    g_input_device_config.adaptive_input = enabled;
    compositor_log_info("Adaptive input processing %s", enabled ? "enabled" : "disabled");
    return COMPOSITOR_OK;
}

// 获取输入捕获模式
CompositorInputCaptureMode compositor_input_manager_get_capture_mode(void) {
    return g_input_capture_mode;
}

// 设置输入捕获模式
void compositor_input_manager_set_capture_mode(CompositorInputCaptureMode mode) {
    g_input_capture_mode = mode;
    compositor_log_debug("Input capture mode set to: %d", mode);
}

// 检查设备类型是否支持
bool compositor_input_manager_is_device_type_supported(CompositorInputDeviceType device_type) {
    // 使用设备工具模块验证设备类型
    return compositor_validate_device_type(device_type) && 
           g_input_device_config.device_type_supported[device_type];
}

// 获取性能统计
int compositor_input_manager_get_performance_stats(CompositorInputPerformanceStats* out_stats) {
    // 使用模块工具验证参数
    if (!compositor_validate_params("compositor_input_manager_get_performance_stats", 
                                   1, (void*[]){out_stats})) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 实际实现中应该从性能模块获取统计信息
    // 这里暂时返回空统计
    memset(out_stats, 0, sizeof(CompositorInputPerformanceStats));
    
    return COMPOSITOR_OK;
}

// 重置性能统计
int compositor_input_manager_reset_performance_stats(void) {
    // 使用模块工具记录日志
    compositor_log_debug("Performance stats reset");
    
    return COMPOSITOR_OK;
}

// 获取设备配置
int compositor_input_manager_get_device_config(CompositorInputDeviceConfig* out_config) {
    // 使用模块工具验证参数
    if (!compositor_validate_params("compositor_input_manager_get_device_config", 
                                   1, (void*[]){out_config})) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 复制当前配置
    *out_config = g_input_device_config;
    
    return COMPOSITOR_OK;
}

// 设置设备配置
int compositor_input_manager_set_device_config(const CompositorInputDeviceConfig* config) {
    // 使用模块工具验证参数
    if (!compositor_validate_params("compositor_input_manager_set_device_config", 
                                   1, (void*[]){(void*)config})) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 更新配置
    g_input_device_config = *config;
    
    // 重新选择活动设备
    g_active_device = compositor_input_manager_get_highest_priority_active_device();
    
    compositor_log_debug("Device configuration updated");
    
    return COMPOSITOR_OK;
}

// 获取设备能力
int compositor_input_manager_get_device_capabilities(int device_id, uint32_t* out_capabilities) {
    // 使用设备工具模块验证参数
    if (!compositor_validate_params("compositor_input_manager_get_device_capabilities", 
                                   2, (void*[]){(void*)(intptr_t)device_id, out_capabilities})) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 使用设备工具模块查找设备
    DeviceSearchResult search_result = compositor_input_device_find_by_id(device_id);
    if (!search_result.found) {
        return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
    }
    
    // 使用设备工具模块获取能力
    *out_capabilities = compositor_input_device_get_capabilities(search_result.device);
    
    return COMPOSITOR_OK;
}

// 获取设备数量
int compositor_input_manager_get_device_count(void) {
    return compositor_input_device_count_all();
}

// 获取已启用设备数量
int compositor_input_manager_get_enabled_device_count(void) {
    return compositor_input_device_count_enabled();
}