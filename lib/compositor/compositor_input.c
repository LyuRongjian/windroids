#include "compositor_input.h"
#include "compositor_window.h"
#include "compositor_utils.h"
#include <string.h>
#include <math.h>

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

// Alt+Tab功能相关静态变量
static bool g_alt_key_pressed = false;
static bool g_window_switching = false;
static int g_selected_window_index = 0;
static void** g_window_list = NULL;
static int g_window_list_count = 0;
static bool* g_window_is_wayland_list = NULL;

// 高级手势识别器配置
static struct {
    int32_t double_tap_timeout;      // 双击超时时间（毫秒）
    int32_t long_press_timeout;      // 长按超时时间（毫秒）
    float tap_threshold;             // 点击阈值（像素）
    float swipe_threshold;           // 滑动阈值（像素）
    float pinch_threshold;           // 捏合阈值（缩放比例）
    float rotation_threshold;        // 旋转阈值（度）
    float velocity_threshold;        // 速度阈值（像素/秒）
} g_gesture_recognizer_config = {
    .double_tap_timeout = 300,
    .long_press_timeout = 500,
    .tap_threshold = 10.0f,
    .swipe_threshold = 50.0f,
    .pinch_threshold = 0.1f,
    .rotation_threshold = 5.0f,
    .velocity_threshold = 100.0f
};

// 手势状态
static struct {
    bool is_active;
    CompositorGestureType type;
    int start_x[10];  // 扩展支持多点触控
    int start_y[10];
    int current_x[10];
    int current_y[10];
    int touch_count;
    int64_t start_time;
    int64_t last_update_time;
    float scale;     // 缩放因子
    float rotation;  // 旋转角度
    float velocity_x; // 速度X分量
    float velocity_y; // 速度Y分量
    float acceleration_x; // 加速度X分量
    float acceleration_y; // 加速度Y分量
    int64_t last_click_time; // 上次点击时间
    float last_click_x;     // 上次点击X坐标
    float last_click_y;     // 上次点击Y坐标
    int click_count;        // 连续点击次数
} g_gesture_state = {0};

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

// 查找指定位置的表面
static void* find_surface_at_position(int x, int y, bool* is_wayland) {
    if (!g_compositor_state || !is_wayland) {
        return NULL;
    }
    
    // 先检查Wayland窗口（从顶层开始）
    for (int i = g_compositor_state->wayland_state.window_count - 1; i >= 0; i--) {
        WaylandWindow* window = g_compositor_state->wayland_state.windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            *is_wayland = true;
            return window;
        }
    }
    
    // 再检查Xwayland窗口
    for (int i = g_compositor_state->xwayland_state.window_count - 1; i >= 0; i--) {
        XwaylandWindowState* window = g_compositor_state->xwayland_state.windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            *is_wayland = false;
            return window;
        }
    }
    
    return NULL;
}

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

// 获取当前时间（毫秒）
static int64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 初始化输入设备
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
    
    log_message(COMPOSITOR_LOG_INFO, "Registered input device: %s (ID: %d, Type: %d)", 
               name, device_id, type);
    
    return COMPOSITOR_OK;
}

// 注销输入设备
int compositor_input_unregister_device(int device_id) {
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            // 释放设备名称
            if (g_input_devices[i].name) {
                free((void*)g_input_devices[i].name);
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
CompositorInputDevice* compositor_input_get_device(int device_id) {
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].device_id == device_id) {
            return &g_input_devices[i];
        }
    }
    return NULL;
}

// 设置设备启用状态
int compositor_input_set_device_enabled(int device_id, bool enabled) {
    CompositorInputDevice* device = compositor_input_get_device(device_id);
    if (!device) {
        return COMPOSITOR_ERROR_DEVICE_NOT_FOUND;
    }
    
    device->enabled = enabled;
    log_message(COMPOSITOR_LOG_DEBUG, "Device %d enabled: %s", device_id, enabled ? "true" : "false");
    
    return COMPOSITOR_OK;
}

// 设置输入捕获模式
void compositor_input_set_capture_mode(CompositorInputCaptureMode mode) {
    g_input_capture_mode = mode;
    log_message(COMPOSITOR_LOG_DEBUG, "Input capture mode set to: %d", mode);
}

// 获取输入捕获模式
CompositorInputCaptureMode compositor_input_get_capture_mode(void) {
    return g_input_capture_mode;
}

// 清理窗口列表资源
static void cleanup_window_list(void) {
    if (g_window_list != NULL) {
        free(g_window_list);
        g_window_list = NULL;
    }
    if (g_window_is_wayland_list != NULL) {
        free(g_window_is_wayland_list);
        g_window_is_wayland_list = NULL;
    }
    g_window_list_count = 0;
    g_selected_window_index = 0;
}

// 收集所有可见窗口
static void collect_visible_windows(void) {
    cleanup_window_list();
    
    // 统计窗口数量
    int count = 0;
    
    // 检查Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            xwayland_state->windows[i]->surface != NULL) {
            count++;
        }
    }
    
    // 检查Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            wayland_state->windows[i]->surface != NULL) {
            count++;
        }
    }
    
    // 分配内存
    if (count > 0) {
        g_window_list = (void**)malloc(count * sizeof(void*));
        g_window_is_wayland_list = (bool*)malloc(count * sizeof(bool));
        g_window_list_count = 0;
        
        // 填充Xwayland窗口
        for (int i = 0; i < xwayland_state->window_count; i++) {
            if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                xwayland_state->windows[i]->surface != NULL) {
                g_window_list[g_window_list_count] = xwayland_state->windows[i];
                g_window_is_wayland_list[g_window_list_count] = false;
                g_window_list_count++;
            }
        }
        
        // 填充Wayland窗口
        for (int i = 0; i < wayland_state->window_count; i++) {
            if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                wayland_state->windows[i]->surface != NULL) {
                g_window_list[g_window_list_count] = wayland_state->windows[i];
                g_window_is_wayland_list[g_window_list_count] = true;
                g_window_list_count++;
            }
        }
    }
}

// 高亮显示选中的窗口
static void highlight_selected_window(void) {
    // 重置所有窗口透明度
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        xwayland_state->windows[i]->opacity = 1.0f;
    }
    
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        wayland_state->windows[i]->opacity = 1.0f;
    }
    
    // 降低非选中窗口的透明度
    if (g_window_list_count > 0 && g_selected_window_index >= 0 && g_selected_window_index < g_window_list_count) {
        for (int i = 0; i < g_window_list_count; i++) {
            if (i != g_selected_window_index) {
                if (g_window_is_wayland_list[i]) {
                    WaylandWindow* window = (WaylandWindow*)g_window_list[i];
                    window->opacity = 0.4f;
                } else {
                    XwaylandWindowState* window = (XwaylandWindowState*)g_window_list[i];
                    window->opacity = 0.4f;
                }
            }
        }
    }
}

// 激活选中的窗口
static void activate_selected_window(void) {
    if (g_window_list_count > 0 && g_selected_window_index >= 0 && g_selected_window_index < g_window_list_count) {
        if (g_window_is_wayland_list[g_selected_window_index]) {
            WaylandWindow* wayland_window = (WaylandWindow*)g_window_list[g_selected_window_index];
            
            // 激活Wayland窗口
            wayland_window_activate(wayland_window);
            
            // 更新全局活动窗口信息
            if (g_compositor_state) {
                g_compositor_state->active_window = wayland_window;
                g_compositor_state->active_window_is_wayland = true;
            }
        } else {
            XwaylandWindowState* xwayland_window = (XwaylandWindowState*)g_window_list[g_selected_window_index];
            
            // 激活Xwayland窗口
            xwayland_window_activate(xwayland_window);
            
            // 更新全局活动窗口信息
            if (g_compositor_state) {
                g_compositor_state->active_window = xwayland_window;
                g_compositor_state->active_window_is_wayland = false;
            }
        }
    }
}

// 处理手势开始
static void handle_gesture_start(CompositorInputEvent* event) {
    if (event->touch_count > 0) {
        // 保留一些状态信息（如上次点击时间），但重置其他状态
        int64_t last_click_time = g_gesture_state.last_click_time;
        float last_click_x = g_gesture_state.last_click_x;
        float last_click_y = g_gesture_state.last_click_y;
        int click_count = g_gesture_state.click_count;
        
        memset(&g_gesture_state, 0, sizeof(g_gesture_state));
        
        // 恢复需要保留的状态
        g_gesture_state.last_click_time = last_click_time;
        g_gesture_state.last_click_x = last_click_x;
        g_gesture_state.last_click_y = last_click_y;
        g_gesture_state.click_count = click_count;
        
        // 设置新的手势状态
        g_gesture_state.is_active = true;
        g_gesture_state.touch_count = event->touch_count;
        g_gesture_state.start_time = get_current_time_ms();
        g_gesture_state.last_update_time = g_gesture_state.start_time;
        g_gesture_state.scale = 1.0f;
        g_gesture_state.rotation = 0.0f;
        g_gesture_state.velocity_x = 0.0f;
        g_gesture_state.velocity_y = 0.0f;
        g_gesture_state.acceleration_x = 0.0f;
        g_gesture_state.acceleration_y = 0.0f;
        
        // 保存触摸点初始位置
        for (int i = 0; i < event->touch_count && i < 10; i++) {
            g_gesture_state.start_x[i] = event->touches[i].x;
            g_gesture_state.start_y[i] = event->touches[i].y;
            g_gesture_state.current_x[i] = event->touches[i].x;
            g_gesture_state.current_y[i] = event->touches[i].y;
        }
        
        // 检测是否为连续点击（如双击、三击）
        if (event->touch_count == 1) {
            int64_t current_time = get_current_time_ms();
            float dx = fabs(event->touches[0].x - last_click_x);
            float dy = fabs(event->touches[0].y - last_click_y);
            
            if (current_time - last_click_time < g_gesture_recognizer_config.double_tap_timeout &&
                dx < g_gesture_recognizer_config.tap_threshold &&
                dy < g_gesture_recognizer_config.tap_threshold) {
                // 连续点击
                g_gesture_state.click_count++;
            } else {
                // 新的点击序列
                g_gesture_state.click_count = 1;
            }
            
            // 记录点击信息
            g_gesture_state.last_click_time = current_time;
            g_gesture_state.last_click_x = event->touches[0].x;
            g_gesture_state.last_click_y = event->touches[0].y;
        }
        
        // 根据触摸点数和上下文确定初始手势类型
        if (event->touch_count == 1) {
            g_gesture_state.type = COMPOSITOR_GESTURE_TYPE_TAP;
        } else if (event->touch_count == 2) {
            g_gesture_state.type = COMPOSITOR_GESTURE_TYPE_PINCH;
        } else if (event->touch_count >= 3) {
            g_gesture_state.type = COMPOSITOR_GESTURE_TYPE_SWIPE;
        }
        
        log_message(COMPOSITOR_LOG_DEBUG, "Gesture started: type=%d, touch_count=%d, click_count=%d", 
                   g_gesture_state.type, g_gesture_state.touch_count, g_gesture_state.click_count);
    }
}

// 计算两点之间的距离
static float calculate_distance(int x1, int y1, int x2, int y2) {
    return sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

// 计算两点之间的角度（度）
static float calculate_angle(int x1, int y1, int x2, int y2) {
    float angle = atan2f(y2 - y1, x2 - x1) * 180.0f / M_PI;
    if (angle < 0) angle += 360.0f;
    return angle;
}

// 计算触摸点的平均位置
static void calculate_average_position(int touch_count, const int* x_coords, const int* y_coords, float* avg_x, float* avg_y) {
    *avg_x = 0.0f;
    *avg_y = 0.0f;
    
    for (int i = 0; i < touch_count; i++) {
        *avg_x += x_coords[i];
        *avg_y += y_coords[i];
    }
    
    *avg_x /= touch_count;
    *avg_y /= touch_count;
}

// 计算手势速度和加速度
static void calculate_gesture_velocity_and_acceleration(float delta_x, float delta_y, int64_t time_delta, 
                                                       float* velocity_x, float* velocity_y, 
                                                       float* acceleration_x, float* acceleration_y) {
    // 计算当前速度（像素/秒）
    float new_velocity_x = (delta_x / time_delta) * 1000.0f;
    float new_velocity_y = (delta_y / time_delta) * 1000.0f;
    
    // 计算加速度（像素/秒²）
    *acceleration_x = (new_velocity_x - *velocity_x) / (time_delta / 1000.0f);
    *acceleration_y = (new_velocity_y - *velocity_y) / (time_delta / 1000.0f);
    
    // 更新速度
    *velocity_x = new_velocity_x;
    *velocity_y = new_velocity_y;
}

// 识别高级手势类型
static void recognize_advanced_gesture(CompositorGestureType* gesture_type, int touch_count, 
                                      float total_distance, float scale_change, float rotation_change) {
    // 如果已有确定的手势类型，保持不变
    if (*gesture_type != COMPOSITOR_GESTURE_TYPE_TAP) {
        return;
    }
    
    // 根据移动距离和触摸点数识别手势
    if (total_distance > g_gesture_recognizer_config.swipe_threshold) {
        if (touch_count == 1) {
            *gesture_type = COMPOSITOR_GESTURE_TYPE_SWIPE;
        } else if (touch_count >= 3) {
            *gesture_type = COMPOSITOR_GESTURE_TYPE_SWIPE;
        }
    }
    
    // 检测捏合手势
    if (touch_count >= 2 && fabs(scale_change - 1.0f) > g_gesture_recognizer_config.pinch_threshold) {
        *gesture_type = COMPOSITOR_GESTURE_TYPE_PINCH;
    }
    
    // 检测旋转手势
    if (touch_count >= 2 && fabs(rotation_change) > g_gesture_recognizer_config.rotation_threshold) {
        *gesture_type = COMPOSITOR_GESTURE_TYPE_ROTATE;
    }
}

// 处理手势更新
static void handle_gesture_update(CompositorInputEvent* event) {
    if (!g_gesture_state.is_active || event->touch_count != g_gesture_state.touch_count) {
        return;
    }
    
    // 记录上次位置用于速度计算
    int last_x[10], last_y[10];
    for (int i = 0; i < event->touch_count && i < 10; i++) {
        last_x[i] = g_gesture_state.current_x[i];
        last_y[i] = g_gesture_state.current_y[i];
        g_gesture_state.current_x[i] = event->touches[i].x;
        g_gesture_state.current_y[i] = event->touches[i].y;
    }
    
    // 计算时间差
    int64_t current_time = get_current_time_ms();
    int64_t time_delta = current_time - g_gesture_state.last_update_time;
    if (time_delta == 0) time_delta = 1; // 避免除零错误
    g_gesture_state.last_update_time = current_time;
    
    // 计算手势参数
    CompositorGestureInfo gesture_info;
    memset(&gesture_info, 0, sizeof(CompositorGestureInfo));
    gesture_info.type = g_gesture_state.type;
    gesture_info.touch_count = g_gesture_state.touch_count;
    
    // 计算平均位置变化
    float avg_start_x, avg_start_y, avg_current_x, avg_current_y;
    calculate_average_position(event->touch_count, g_gesture_state.start_x, g_gesture_state.start_y, &avg_start_x, &avg_start_y);
    calculate_average_position(event->touch_count, g_gesture_state.current_x, g_gesture_state.current_y, &avg_current_x, &avg_current_y);
    
    gesture_info.delta_x = (int)(avg_current_x - avg_start_x);
    gesture_info.delta_y = (int)(avg_current_y - avg_start_y);
    
    // 计算总移动距离
    float total_distance = sqrtf(gesture_info.delta_x * gesture_info.delta_x + gesture_info.delta_y * gesture_info.delta_y);
    
    // 计算当前帧的移动距离（用于速度计算）
    float frame_delta_x = 0.0f, frame_delta_y = 0.0f;
    for (int i = 0; i < event->touch_count && i < 10; i++) {
        frame_delta_x += g_gesture_state.current_x[i] - last_x[i];
        frame_delta_y += g_gesture_state.current_y[i] - last_y[i];
    }
    frame_delta_x /= event->touch_count;
    frame_delta_y /= event->touch_count;
    
    // 更新速度和加速度
    calculate_gesture_velocity_and_acceleration(frame_delta_x, frame_delta_y, time_delta, 
                                              &g_gesture_state.velocity_x, &g_gesture_state.velocity_y,
                                              &g_gesture_state.acceleration_x, &g_gesture_state.acceleration_y);
    
    // 根据手势类型计算不同参数
    if (event->touch_count >= 2) {
        // 计算捏合缩放（使用前两个触摸点）
        float start_dist = calculate_distance(
            g_gesture_state.start_x[0], g_gesture_state.start_y[0],
            g_gesture_state.start_x[1], g_gesture_state.start_y[1]
        );
        float current_dist = calculate_distance(
            g_gesture_state.current_x[0], g_gesture_state.current_y[0],
            g_gesture_state.current_x[1], g_gesture_state.current_y[1]
        );
        
        if (start_dist > 0) {
            g_gesture_state.scale = current_dist / start_dist;
            gesture_info.scale = g_gesture_state.scale;
        }
        
        // 计算旋转角度
        float start_angle = calculate_angle(
            g_gesture_state.start_x[0], g_gesture_state.start_y[0],
            g_gesture_state.start_x[1], g_gesture_state.start_y[1]
        );
        float current_angle = calculate_angle(
            g_gesture_state.current_x[0], g_gesture_state.current_y[0],
            g_gesture_state.current_x[1], g_gesture_state.current_y[1]
        );
        
        g_gesture_state.rotation = current_angle - start_angle;
        gesture_info.rotation = g_gesture_state.rotation;
    }
    
    // 重新评估手势类型
    recognize_advanced_gesture(&gesture_info.type, event->touch_count, 
                              total_distance, gesture_info.scale, gesture_info.rotation);
    g_gesture_state.type = gesture_info.type;
    
    log_message(COMPOSITOR_LOG_DEBUG, 
               "Gesture update: type=%d, scale=%.2f, rotation=%.2f, dx=%d, dy=%d, velocity=(%.2f,%.2f)", 
               gesture_info.type, gesture_info.scale, gesture_info.rotation, 
               gesture_info.delta_x, gesture_info.delta_y,
               g_gesture_state.velocity_x, g_gesture_state.velocity_y);
    
    // 查找手势作用的窗口
    bool is_wayland = false;
    void* surface = find_surface_at_position((int)avg_current_x, (int)avg_current_y, &is_wayland);
    
    // 处理窗口操作手势
    if (surface) {
        // 根据手势类型应用不同的窗口操作
        switch (gesture_info.type) {
            case COMPOSITOR_GESTURE_TYPE_PINCH:
                // 实现窗口缩放
                if (g_compositor_state->config.enable_window_gestures) {
                    // 注意：实际应用中需要添加边界检查和最小/最大缩放比例
                    float scale_factor = gesture_info.scale;
                    // 可以添加一个窗口缩放函数来实现这个功能
                }
                break;
                
            case COMPOSITOR_GESTURE_TYPE_ROTATE:
                // 实现窗口旋转
                if (g_compositor_state->config.enable_window_gestures) {
                    // 可以添加一个窗口旋转函数来实现这个功能
                }
                break;
                
            case COMPOSITOR_GESTURE_TYPE_SWIPE:
                // 滑动操作移动窗口
                if (g_compositor_state->config.enable_window_gestures) {
                    int new_x, new_y;
                    if (is_wayland) {
                        WaylandWindow* window = (WaylandWindow*)surface;
                        new_x = window->x + gesture_info.delta_x;
                        new_y = window->y + gesture_info.delta_y;
                        
                        // 改进的边界检查
                        if (new_x < 0) new_x = 0;
                        if (new_y < 0) new_y = 0;
                        if (new_x > g_compositor_state->width - window->width) {
                            new_x = g_compositor_state->width - window->width;
                        }
                        if (new_y > g_compositor_state->height - window->height) {
                            new_y = g_compositor_state->height - window->height;
                        }
                        
                        window->x = new_x;
                        window->y = new_y;
                    } else {
                        XwaylandWindowState* window = (XwaylandWindowState*)surface;
                        new_x = window->x + gesture_info.delta_x;
                        new_y = window->y + gesture_info.delta_y;
                        
                        // 改进的边界检查
                        if (new_x < 0) new_x = 0;
                        if (new_y < 0) new_y = 0;
                        if (new_x > g_compositor_state->width - window->width) {
                            new_x = g_compositor_state->width - window->width;
                        }
                        if (new_y > g_compositor_state->height - window->height) {
                            new_y = g_compositor_state->height - window->height;
                        }
                        
                        window->x = new_x;
                        window->y = new_y;
                    }
                    
                    // 标记脏区域
                    compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
                }
                break;
        }
    }
    
    // 触发手势更新事件给监听器
    if (g_compositor_state && g_compositor_state->input_listener) {
        g_compositor_state->input_listener(&gesture_info);
    }
}

// 处理手势结束
static void handle_gesture_end(void) {
    if (!g_gesture_state.is_active) {
        return;
    }
    
    int64_t duration = get_current_time_ms() - g_gesture_state.start_time;
    
    // 检测点击类型（单击、双击、长按等）
    if (g_gesture_state.type == COMPOSITOR_GESTURE_TYPE_TAP) {
        // 计算总移动距离
        float total_distance = 0.0f;
        for (int i = 0; i < g_gesture_state.touch_count && i < 10; i++) {
            float dx = g_gesture_state.current_x[i] - g_gesture_state.start_x[i];
            float dy = g_gesture_state.current_y[i] - g_gesture_state.start_y[i];
            total_distance += sqrtf(dx * dx + dy * dy);
        }
        total_distance /= g_gesture_state.touch_count;
        
        // 如果移动距离很小，可能是点击
        if (total_distance < g_gesture_recognizer_config.tap_threshold) {
            if (duration >= g_gesture_recognizer_config.long_press_timeout) {
                // 长按手势
                log_message(COMPOSITOR_LOG_DEBUG, "Long press detected: duration=%lldms", duration);
                
                // 触发长按事件
                CompositorGestureInfo long_press_info;
                memset(&long_press_info, 0, sizeof(CompositorGestureInfo));
                long_press_info.type = COMPOSITOR_GESTURE_TYPE_PINCH; // 使用已有的手势类型
                long_press_info.touch_count = g_gesture_state.touch_count;
                long_press_info.delta_x = g_gesture_state.current_x[0] - g_gesture_state.start_x[0];
                long_press_info.delta_y = g_gesture_state.current_y[0] - g_gesture_state.start_y[0];
                
                // 处理长按菜单或其他功能
                if (g_compositor_state && g_compositor_state->input_listener) {
                    g_compositor_state->input_listener(&long_press_info);
                }
            } else if (g_gesture_state.click_count >= 2) {
                // 双击或更多点击
                log_message(COMPOSITOR_LOG_DEBUG, "Multi-tap detected: count=%d", g_gesture_state.click_count);
                
                // 触发双击事件
                CompositorGestureInfo tap_info;
                memset(&tap_info, 0, sizeof(CompositorGestureInfo));
                tap_info.type = COMPOSITOR_GESTURE_TYPE_TWO_FINGER_TAP; // 使用已有的手势类型
                tap_info.touch_count = g_gesture_state.touch_count;
                
                if (g_compositor_state && g_compositor_state->input_listener) {
                    g_compositor_state->input_listener(&tap_info);
                }
            } else {
                // 单击
                log_message(COMPOSITOR_LOG_DEBUG, "Single tap detected");
            }
        }
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Gesture ended: type=%d, duration=%lldms, velocity=(%.2f,%.2f)", 
               g_gesture_state.type, duration, g_gesture_state.velocity_x, g_gesture_state.velocity_y);
    
    // 重置手势活跃状态，但保留一些状态信息（如点击计数）
    g_gesture_state.is_active = false;
    g_gesture_state.type = COMPOSITOR_GESTURE_TYPE_NONE;
    g_gesture_state.touch_count = 0;
    g_gesture_state.scale = 1.0f;
    g_gesture_state.rotation = 0.0f;
    g_gesture_state.velocity_x = 0.0f;
    g_gesture_state.velocity_y = 0.0f;
    g_gesture_state.acceleration_x = 0.0f;
    g_gesture_state.acceleration_y = 0.0f;
}

// 处理输入事件
void process_input_events() {
    if (!g_compositor_state) return;
    
    CompositorInputEvent event;
    while (compositor_input_get_next_event(&event)) {
        // 处理不同类型的输入事件
        switch (event.type) {
            case COMPOSITOR_INPUT_EVENT_MOUSE_MOTION:
                process_mouse_motion_event(&event);
                break;
                
            case COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON:
                process_mouse_button_event(&event);
                break;
                
            case COMPOSITOR_INPUT_EVENT_TOUCH:
                process_touch_event(&event);
                break;
                
            case COMPOSITOR_INPUT_EVENT_GESTURE:
                process_gesture_event(&event);
                break;
                
            default:
                if (g_compositor_state && g_compositor_state->config.debug_mode) {
                    log_message(COMPOSITOR_LOG_DEBUG, "Unhandled input event type: %d", event.type);
                }
                break;
        }
    }
}

// 处理鼠标移动事件
void process_mouse_motion_event(CompositorInputEvent* event) {
    if (!g_compositor_state) return;
    
    // 如果正在拖动窗口，更新窗口位置
    if (g_compositor_state->is_dragging && g_compositor_state->dragging_window) {
        int new_x = event->mouse.x - g_compositor_state->drag_offset_x;
        int new_y = event->mouse.y - g_compositor_state->drag_offset_y;
        
        // 限制窗口不超出屏幕边界（可配置是否启用）
        if (g_compositor_state->config.restrict_window_bounds) {
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x + g_compositor_state->dragging_window->width > g_compositor_state->width) {
                new_x = g_compositor_state->width - g_compositor_state->dragging_window->width;
            }
            if (new_y + g_compositor_state->dragging_window->height > g_compositor_state->height) {
                new_y = g_compositor_state->height - g_compositor_state->dragging_window->height;
            }
        }
        
        // 更新窗口位置
        g_compositor_state->dragging_window->x = new_x;
        g_compositor_state->dragging_window->y = new_y;
        
        // 标记窗口区域为脏
        compositor_mark_dirty_rect(new_x, new_y, 
                                 g_compositor_state->dragging_window->width,
                                 g_compositor_state->dragging_window->height);
        
        // 触发重绘
        g_compositor_state->needs_redraw = true;
    }
}

// 处理鼠标按钮事件
void process_mouse_button_event(CompositorInputEvent* event) {
    if (!g_compositor_state) return;
    
    // 只有左键点击才处理
    if (event->mouse_button.button != COMPOSITOR_MOUSE_BUTTON_LEFT) {
        return;
    }
    
    if (event->mouse_button.pressed) {
        // 查找点击位置的窗口
        bool is_wayland = false;
        void* window = find_surface_at_position(event->mouse_button.x, 
                                              event->mouse_button.y, &is_wayland);
        
        if (window) {
            // 记录拖动信息
            g_compositor_state->dragging_window = window;
            g_compositor_state->is_dragging = true;
            g_compositor_state->drag_offset_x = event->mouse_button.x - ((WaylandWindow*)window)->x;
            g_compositor_state->drag_offset_y = event->mouse_button.y - ((WaylandWindow*)window)->y;
            
            // 更新活动窗口
            g_compositor_state->active_window = window;
            g_compositor_state->active_window_is_wayland = is_wayland;
        } else {
            // 点击桌面背景，取消拖动
            g_compositor_state->is_dragging = false;
            g_compositor_state->dragging_window = NULL;
            g_compositor_state->active_window = NULL;
        }
    } else {
        // 释放鼠标按钮，结束拖动
        g_compositor_state->is_dragging = false;
    }
}

// 处理触摸事件
void process_touch_event(CompositorInputEvent* event) {
    // 这里可以添加更多的触摸事件处理逻辑
}

// 处理手势事件
void process_gesture_event(CompositorInputEvent* event) {
    if (!g_compositor_state) return;
    
    // 处理不同类型的手势事件
    if (event->gesture.type == COMPOSITOR_GESTURE_TAP) {
        // 处理点击事件
        int x = event->gesture.x;
        int y = event->gesture.y;
        
        // 查找点击位置的窗口
        bool is_wayland = false;
        void* window = find_surface_at_position(x, y, &is_wayland);
        
        if (window) {
            // 更新活动窗口
            g_compositor_state->active_window = window;
            g_compositor_state->active_window_is_wayland = is_wayland;
            
            // 如果是双击，最大化/还原窗口
            if (event->gesture.tap_count == 2) {
                const char* title = is_wayland ? 
                    ((WaylandWindow*)window)->title : 
                    ((XwaylandWindowState*)window)->title;
                
                if (is_wayland) {
                    WaylandWindow* wayland_window = (WaylandWindow*)window;
                    if (wayland_window->state == WINDOW_STATE_MAXIMIZED) {
                        compositor_restore_window(title);
                    } else {
                        compositor_maximize_window(title);
                    }
                } else {
                    XwaylandWindowState* xway_window = (XwaylandWindowState*)window;
                    if (xway_window->state == WINDOW_STATE_MAXIMIZED) {
                        compositor_restore_window(title);
                    } else {
                        compositor_maximize_window(title);
                    }
                }
            }
        } else {
            // 点击桌面背景，取消活动窗口
            g_compositor_state->active_window = NULL;
        }
    } else if (event->gesture.type == COMPOSITOR_GESTURE_SWIPE) {
        // 处理滑动事件
        // 水平滑动可以用于工作区切换
        if (event->gesture.swipe_direction == COMPOSITOR_GESTURE_SWIPE_LEFT || 
            event->gesture.swipe_direction == COMPOSITOR_GESTURE_SWIPE_RIGHT) {
            int current_workspace = g_compositor_state->active_workspace_index;
            int new_workspace = current_workspace + 
                (event->gesture.swipe_direction == COMPOSITOR_GESTURE_SWIPE_LEFT ? 1 : -1);
            
            // 循环切换工作区
            if (new_workspace < 0) {
                new_workspace = g_compositor_state->workspace_count - 1;
            } else if (new_workspace >= g_compositor_state->workspace_count) {
                new_workspace = 0;
            }
            
            compositor_switch_workspace(new_workspace);
        }
    } else if (event->gesture.type == COMPOSITOR_GESTURE_PINCH) {
        // 处理捏合手势（可用于窗口管理操作）
        // 这里可以根据捏合比例和方向实现窗口缩放等功能
    }
}

// 处理输入事件（新接口）
// 静态辅助函数：处理轨迹球输入
static void handle_trackball_event(CompositorInputEvent* event) {
    log_message(COMPOSITOR_LOG_DEBUG, "Trackball: dx=%d, dy=%d", event->scroll_dx, event->scroll_dy);
    
    // 更新设备支持标志
    g_input_device_config.device_type_supported[COMPOSITOR_DEVICE_TYPE_TRACKBALL] = true;
    
    // 处理轨迹球移动，映射为鼠标移动
    int new_x = g_compositor_state->mouse_x + event->scroll_dx;
    int new_y = g_compositor_state->mouse_y + event->scroll_dy;
    
    // 边界检查
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x > g_compositor_state->width - 1) new_x = g_compositor_state->width - 1;
    if (new_y > g_compositor_state->height - 1) new_y = g_compositor_state->height - 1;
    
    // 更新鼠标位置
    g_compositor_state->mouse_x = new_x;
    g_compositor_state->mouse_y = new_y;
    
    // 模拟鼠标移动事件
    CompositorInputEvent mouse_event;
    memset(&mouse_event, 0, sizeof(CompositorInputEvent));
    mouse_event.type = COMPOSITOR_INPUT_EVENT_MOUSE_MOTION;
    mouse_event.x = new_x;
    mouse_event.y = new_y;
    mouse_event.state = COMPOSITOR_INPUT_STATE_MOTION;
    mouse_event.device_id = event->device_id;
    
    compositor_handle_input_event(&mouse_event);
}

// 静态辅助函数：处理触控板特定手势
static void handle_touchpad_gesture(CompositorInputEvent* event) {
    log_message(COMPOSITOR_LOG_DEBUG, "Touchpad gesture: type=%d, fingers=%d", 
               event->gesture_type, event->touch_count);
    
    // 根据手指数量和手势类型执行不同操作
    switch (event->gesture_type) {
        case COMPOSITOR_GESTURE_TYPE_SWIPE:
            // 三指滑动：工作区切换
            if (event->touch_count == 3) {
                if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_LEFT) {
                    // 切换到下一个工作区
                    compositor_switch_workspace((g_compositor_state->active_workspace + 1) % g_compositor_state->workspace_count);
                } else if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_RIGHT) {
                    // 切换到上一个工作区
                    compositor_switch_workspace((g_compositor_state->active_workspace - 1 + g_compositor_state->workspace_count) % g_compositor_state->workspace_count);
                } else if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_UP) {
                    // 显示工作区概览
                    compositor_show_workspace_overview();
                } else if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_DOWN) {
                    // 隐藏工作区概览
                    compositor_hide_workspace_overview();
                }
            }
            // 四指滑动：窗口管理
            else if (event->touch_count == 4) {
                if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_UP) {
                    // 最大化窗口
                    if (g_compositor_state->active_window) {
                        compositor_maximize_window(g_compositor_state->active_window, g_compositor_state->active_window_is_wayland);
                    }
                } else if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_DOWN) {
                    // 最小化窗口
                    if (g_compositor_state->active_window) {
                        compositor_minimize_window(g_compositor_state->active_window, g_compositor_state->active_window_is_wayland);
                    }
                }
            }
            break;
            
        case COMPOSITOR_GESTURE_TYPE_TAP:
            // 两指点击：右键菜单
            if (event->touch_count == 2) {
                // 模拟鼠标右键点击
                CompositorInputEvent mouse_event;
                memset(&mouse_event, 0, sizeof(CompositorInputEvent));
                mouse_event.type = COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON;
                mouse_event.x = event->x;
                mouse_event.y = event->y;
                mouse_event.button = 3; // 右键
                mouse_event.state = COMPOSITOR_INPUT_STATE_PRESSED;
                mouse_event.device_id = event->device_id;
                compositor_handle_input_event(&mouse_event);
                
                // 立即释放
                mouse_event.state = COMPOSITOR_INPUT_STATE_RELEASED;
                compositor_handle_input_event(&mouse_event);
            }
            // 三指点击：显示窗口菜单或任务切换器
            else if (event->touch_count == 3) {
                // 开始窗口切换
                g_window_switching = true;
                collect_visible_windows();
                g_selected_window_index = 0;
                highlight_selected_window();
                compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            break;
    }
}

// 增强的窗口拖动处理
static void handle_window_drag(int x, int y) {
    if (!g_compositor_state->dragging || !g_compositor_state->drag_window) {
        return;
    }
    
    // 计算新位置
    int new_x = g_compositor_state->drag_start_x + (x - g_compositor_state->mouse_start_x);
    int new_y = g_compositor_state->drag_start_y + (y - g_compositor_state->mouse_start_y);
    
    // 边界检查
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    int max_width = g_compositor_state->width - g_compositor_state->drag_window_width - WINDOW_BORDER_WIDTH * 2;
    int max_height = g_compositor_state->height - g_compositor_state->drag_window_height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT;
    if (new_x > max_width) new_x = max_width;
    if (new_y > max_height) new_y = max_height;
    
    // 工作区边缘检测：在边缘停留触发工作区切换
    const int EDGE_THRESHOLD = 50; // 边缘阈值（像素）
    const int EDGE_DELAY = 500;    // 触发延迟（毫秒）
    static int64_t edge_enter_time = 0;
    static int edge_workspace = -1;
    
    // 检查是否在左边缘
    if (new_x < EDGE_THRESHOLD && g_compositor_state->config.wraparound_workspaces) {
        if (edge_workspace != (g_compositor_state->active_workspace - 1 + g_compositor_state->workspace_count) % g_compositor_state->workspace_count) {
            edge_enter_time = get_current_time_ms();
            edge_workspace = (g_compositor_state->active_workspace - 1 + g_compositor_state->workspace_count) % g_compositor_state->workspace_count;
        } else if (get_current_time_ms() - edge_enter_time > EDGE_DELAY) {
            // 切换工作区并继续拖动
            compositor_switch_workspace(edge_workspace);
            // 调整窗口位置到新工作区右侧
            new_x = g_compositor_state->width - g_compositor_state->drag_window_width - WINDOW_BORDER_WIDTH * 2 - EDGE_THRESHOLD;
            g_compositor_state->drag_start_x = new_x;
            g_compositor_state->mouse_start_x = x;
        }
    }
    // 检查是否在右边缘
    else if (new_x > max_width - EDGE_THRESHOLD && g_compositor_state->config.wraparound_workspaces) {
        if (edge_workspace != (g_compositor_state->active_workspace + 1) % g_compositor_state->workspace_count) {
            edge_enter_time = get_current_time_ms();
            edge_workspace = (g_compositor_state->active_workspace + 1) % g_compositor_state->workspace_count;
        } else if (get_current_time_ms() - edge_enter_time > EDGE_DELAY) {
            // 切换工作区并继续拖动
            compositor_switch_workspace(edge_workspace);
            // 调整窗口位置到新工作区左侧
            new_x = EDGE_THRESHOLD;
            g_compositor_state->drag_start_x = new_x;
            g_compositor_state->mouse_start_x = x;
        }
    } else {
        edge_workspace = -1;
    }
    
    // 更新窗口位置
    if (g_compositor_state->drag_is_wayland_window) {
        WaylandWindow* window = (WaylandWindow*)g_compositor_state->drag_window;
        window->x = new_x;
        window->y = new_y;
    } else {
        XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state->drag_window;
        window->x = new_x;
        window->y = new_y;
    }
    
    // 标记脏区域
    compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
}

// 增强的键盘快捷键处理
static void handle_enhanced_keyboard_shortcuts(int keycode, int state, int modifiers) {
    // 工作区快捷键
    if (modifiers == COMPOSITOR_MODIFIER_CTRL_ALT && state == COMPOSITOR_INPUT_STATE_PRESSED) {
        // Ctrl+Alt+数字：切换到对应工作区
        if (keycode >= 10 && keycode <= 19) { // 数字1-0
            int workspace_index = (keycode - 10) % g_compositor_state->workspace_count;
            compositor_switch_workspace(workspace_index);
        }
    }
    
    // Ctrl+Alt+Shift+数字：将当前窗口移动到对应工作区
    if (modifiers == (COMPOSITOR_MODIFIER_CTRL_ALT | COMPOSITOR_MODIFIER_SHIFT) && state == COMPOSITOR_INPUT_STATE_PRESSED) {
        if (keycode >= 10 && keycode <= 19 && g_compositor_state->active_window) { // 数字1-0
            int workspace_index = (keycode - 10) % g_compositor_state->workspace_count;
            compositor_move_window_to_workspace_by_ptr(g_compositor_state->active_window, 
                                                     g_compositor_state->active_window_is_wayland,
                                                     workspace_index);
        }
    }
    
    // 窗口管理快捷键
    if (modifiers == COMPOSITOR_MODIFIER_ALT && state == COMPOSITOR_INPUT_STATE_PRESSED) {
        // Alt+Enter：全屏切换
        if (keycode == 36) {
            if (g_compositor_state->active_window) {
                bool is_fullscreen = false;
                if (g_compositor_state->active_window_is_wayland) {
                    WaylandWindow* window = (WaylandWindow*)g_compositor_state->active_window;
                    is_fullscreen = (window->state == WINDOW_STATE_FULLSCREEN);
                    if (is_fullscreen) {
                        wayland_window_exit_fullscreen(window);
                    } else {
                        wayland_window_enter_fullscreen(window);
                    }
                } else {
                    XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state->active_window;
                    is_fullscreen = (window->state == WINDOW_STATE_FULLSCREEN);
                    if (is_fullscreen) {
                        xwayland_window_exit_fullscreen(window);
                    } else {
                        xwayland_window_enter_fullscreen(window);
                    }
                }
            }
        }
        
        // Alt+F1：显示应用程序菜单
        else if (keycode == 67) {
            compositor_show_application_menu();
        }
    }
    
    // 平铺快捷键
    if (modifiers == (COMPOSITOR_MODIFIER_SUPER | COMPOSITOR_MODIFIER_SHIFT) && state == COMPOSITOR_INPUT_STATE_PRESSED) {
        switch (keycode) {
            case 111: // 上箭头 - 垂直平铺
                compositor_tile_windows(TILE_MODE_VERTICAL);
                break;
            case 116: // 下箭头 - 水平平铺
                compositor_tile_windows(TILE_MODE_HORIZONTAL);
                break;
            case 32:  // 空格键 - 网格平铺
                compositor_tile_windows(TILE_MODE_GRID);
                break;
        }
    }
}

int compositor_handle_input_event(CompositorInputEvent* event) {
    if (!g_compositor_state || !event) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 检查输入设备是否启用
    if (event->device_id != -1) {
        CompositorInputDevice* device = compositor_input_get_device(event->device_id);
        if (device && !device->enabled) {
            return COMPOSITOR_OK;  // 设备已禁用，忽略事件
        }
        // 设置活动设备
        g_active_device = device;
    }
    
    // 根据输入捕获模式过滤事件
    if (g_input_capture_mode == COMPOSITOR_INPUT_CAPTURE_MODE_DISABLED) {
        return COMPOSITOR_OK;  // 输入已禁用
    }
    
    // 性能优化：批量事件处理检测
    static int64_t last_event_time = 0;
    static int event_batch_count = 0;
    int64_t current_time = get_current_time_ms();
    
    // 事件批处理计数
    if (current_time - last_event_time < 5) {  // 5ms内的事件视为同一批次
        event_batch_count++;
    } else {
        event_batch_count = 1;
    }
    last_event_time = current_time;
    
    // 调试日志 - 减少高频事件的日志输出
    if (event->type != COMPOSITOR_INPUT_EVENT_MOUSE_MOTION || event_batch_count % 10 == 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "Handling input event: type=%d, device_id=%d", 
                   event->type, event->device_id);
    }
    
    // 更新全局鼠标位置
    if (event->type == COMPOSITOR_INPUT_EVENT_MOUSE_MOTION || 
        event->type == COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON ||
        event->type == COMPOSITOR_INPUT_EVENT_PEN) {
        g_compositor_state->mouse_x = event->x;
        g_compositor_state->mouse_y = event->y;
    }
    
    // 处理不同类型的输入事件
    switch (event->type) {
        case COMPOSITOR_INPUT_EVENT_MOUSE_MOTION:
            // 鼠标移动事件
            if (event_batch_count % 10 == 0) {  // 减少高频事件日志
                log_message(COMPOSITOR_LOG_DEBUG, "Mouse motion: x=%d, y=%d", event->x, event->y);
            }
            
            // 使用增强的窗口拖动处理
            handle_window_drag(event->x, event->y);
            
            // 鼠标悬停效果
            if (g_compositor_state->config.enable_hover_effects) {
                bool is_wayland = false;
                void* surface = find_surface_at_position(event->x, event->y, &is_wayland);
                if (surface) {
                    // 实现窗口悬停高亮或其他效果
                }
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON:
            // 鼠标按钮事件
            log_message(COMPOSITOR_LOG_DEBUG, "Mouse button: button=%d, state=%d, x=%d, y=%d", 
                       event->button, event->state, event->x, event->y);
            
            if (event->state == COMPOSITOR_INPUT_STATE_PRESSED) {
                // 鼠标按下，查找点击的窗口
                bool is_wayland = false;
                void* surface = find_surface_at_position(event->x, event->y, &is_wayland);
                if (surface) {
                    // 激活点击的窗口
                    if (is_wayland) {
                        WaylandWindow* window = (WaylandWindow*)surface;
                        wayland_window_activate(window);
                        g_compositor_state->active_window = window;
                        g_compositor_state->active_window_is_wayland = true;
                    } else {
                        XwaylandWindowState* window = (XwaylandWindowState*)surface;
                        xwayland_window_activate(window);
                        g_compositor_state->active_window = window;
                        g_compositor_state->active_window_is_wayland = false;
                    }
                    
                    // 标记脏区域
                    compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
                }
                
                // 检测窗口拖动起始
                if (surface) {
                    bool is_titlebar = false;
                    // 检查是否点击了标题栏区域
                    if (is_wayland) {
                        WaylandWindow* window = (WaylandWindow*)surface;
                        is_titlebar = (event->y >= window->y && 
                                      event->y < window->y + WINDOW_TITLEBAR_HEIGHT &&
                                      event->x >= window->x && 
                                      event->x < window->x + window->width);
                    } else {
                        XwaylandWindowState* window = (XwaylandWindowState*)surface;
                        is_titlebar = (event->y >= window->y && 
                                      event->y < window->y + WINDOW_TITLEBAR_HEIGHT &&
                                      event->x >= window->x && 
                                      event->x < window->x + window->width);
                    }
                    
                    // 开始拖动窗口
                    if (is_titlebar && event->button == 1) { // 左键
                        g_compositor_state->dragging = true;
                        g_compositor_state->drag_window = surface;
                        g_compositor_state->drag_is_wayland_window = is_wayland;
                        g_compositor_state->drag_start_x = is_wayland ? 
                                                         ((WaylandWindow*)surface)->x : 
                                                         ((XwaylandWindowState*)surface)->x;
                        g_compositor_state->drag_start_y = is_wayland ? 
                                                         ((WaylandWindow*)surface)->y : 
                                                         ((XwaylandWindowState*)surface)->y;
                        g_compositor_state->drag_window_width = is_wayland ? 
                                                             ((WaylandWindow*)surface)->width : 
                                                             ((XwaylandWindowState*)surface)->width;
                        g_compositor_state->drag_window_height = is_wayland ? 
                                                              ((WaylandWindow*)surface)->height : 
                                                              ((XwaylandWindowState*)surface)->height;
                        g_compositor_state->mouse_start_x = event->x;
                        g_compositor_state->mouse_start_y = event->y;
                    }
                }
            } else if (event->state == COMPOSITOR_INPUT_STATE_RELEASED) {
                // 鼠标释放，结束拖动
                if (event->button == 1 && g_compositor_state->dragging) { // 左键释放
                    g_compositor_state->dragging = false;
                    g_compositor_state->drag_window = NULL;
                }
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_KEYBOARD:
            // 键盘事件
            log_message(COMPOSITOR_LOG_DEBUG, "Keyboard: keycode=%d, state=%d, modifiers=%d", 
                       event->keycode, event->state, event->modifiers);
            
            // Alt键处理
            if (event->keycode == 56 || event->keycode == 184) { // Alt键 (左右)
                if (event->state == COMPOSITOR_INPUT_STATE_PRESSED) {
                    g_alt_key_pressed = true;
                } else if (event->state == COMPOSITOR_INPUT_STATE_RELEASED) {
                    g_alt_key_pressed = false;
                    
                    // 当Alt键释放时，如果正在窗口切换状态，激活选中的窗口
                    if (g_window_switching) {
                        activate_selected_window();
                        g_window_switching = false;
                        cleanup_window_list();
                        
                        // 重置所有窗口透明度
                        for (int i = 0; i < g_compositor_state->xwayland_state.window_count; i++) {
                            g_compositor_state->xwayland_state.windows[i]->opacity = 1.0f;
                        }
                        
                        for (int i = 0; i < g_compositor_state->wayland_state.window_count; i++) {
                            g_compositor_state->wayland_state.windows[i]->opacity = 1.0f;
                        }
                        
                        compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
                    }
                }
            }
            
            // Tab键处理 (Alt+Tab)
            if (event->keycode == 15 && event->state == COMPOSITOR_INPUT_STATE_PRESSED && g_alt_key_pressed) {
                // 开始窗口切换或切换到下一个窗口
                if (!g_window_switching) {
                    g_window_switching = true;
                    collect_visible_windows();
                    g_selected_window_index = 0;
                } else {
                    // 循环选择下一个窗口
                    g_selected_window_index = (g_selected_window_index + 1) % g_window_list_count;
                }
                
                // 高亮显示选中的窗口
                highlight_selected_window();
                compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            
            // Alt+F4 关闭窗口
            if (event->keycode == 62 && event->state == COMPOSITOR_INPUT_STATE_PRESSED && g_alt_key_pressed) {
                // 关闭当前活动窗口
                if (g_compositor_state->active_window) {
                    if (g_compositor_state->active_window_is_wayland) {
                        WaylandWindow* window = (WaylandWindow*)g_compositor_state->active_window;
                        wayland_window_close(window);
                    } else {
                        XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state->active_window;
                        xwayland_window_close(window);
                    }
                }
            }
            
            // 增强的键盘快捷键处理
            handle_enhanced_keyboard_shortcuts(event->keycode, event->state, event->modifiers);
            break;
            
        case COMPOSITOR_INPUT_EVENT_TOUCH:
            // 触摸事件
            log_message(COMPOSITOR_LOG_DEBUG, "Touch: count=%d, state=%d", 
                       event->touch_count, event->state);
            
            // 标记触摸设备支持
            g_input_device_config.device_type_supported[COMPOSITOR_DEVICE_TYPE_TOUCHSCREEN] = true;
            
            // 更新最大同时触摸点数
            if (event->touch_count > g_input_device_config.max_simultaneous_touches) {
                g_input_device_config.max_simultaneous_touches = event->touch_count;
            }
            
            // 手势处理
            if (event->state == COMPOSITOR_INPUT_STATE_PRESSED) {
                handle_gesture_start(event);
            } else if (event->state == COMPOSITOR_INPUT_STATE_MOTION) {
                handle_gesture_update(event);
            } else if (event->state == COMPOSITOR_INPUT_STATE_RELEASED) {
                handle_gesture_end();
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_PEN:
            // 触控笔事件 - 调用专用处理函数
            handle_pen_event(event);
            break;
            
        case COMPOSITOR_INPUT_EVENT_JOYSTICK:
        case COMPOSITOR_INPUT_EVENT_GAMEPAD:
            // 游戏手柄/摇杆事件 - 调用专用处理函数
            handle_gamepad_event(event);
            break;
            
        case COMPOSITOR_INPUT_EVENT_SCROLL:
            // 滚轮/滚动事件
            log_message(COMPOSITOR_LOG_DEBUG, "Scroll: dx=%d, dy=%d, direction=%d", 
                       event->scroll_dx, event->scroll_dy, event->scroll_direction);
            
            // 查找鼠标位置下的窗口并传递滚动事件
            bool is_wayland = false;
            void* surface = find_surface_at_position(g_compositor_state->mouse_x, g_compositor_state->mouse_y, &is_wayland);
            if (surface) {
                // 传递滚动事件到窗口
                if (is_wayland) {
                    WaylandWindow* window = (WaylandWindow*)surface;
                    wayland_window_handle_scroll(window, event->scroll_dx, event->scroll_dy, event->scroll_direction);
                } else {
                    XwaylandWindowState* window = (XwaylandWindowState*)surface;
                    xwayland_window_handle_scroll(window, event->scroll_dx, event->scroll_dy, event->scroll_direction);
                }
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_GESTURE:
            // 手势事件
            log_message(COMPOSITOR_LOG_DEBUG, "Gesture: type=%d, scale=%.2f, rotation=%.2f, x=%d, y=%d, fingers=%d", 
                       event->gesture_type, event->gesture_scale, event->gesture_rotation, 
                       event->x, event->y, event->touch_count);
            
            // 如果是触控板手势，使用专门的处理函数
            if (event->device_type == COMPOSITOR_DEVICE_TYPE_TOUCHPAD) {
                handle_touchpad_gesture(event);
            } else {
                // 查找手势作用的窗口
                bool is_wayland = false;
                void* surface = find_surface_at_position(event->x, event->y, &is_wayland);
                
                if (surface) {
                    // 根据手势类型处理
                    switch (event->gesture_type) {
                        case COMPOSITOR_GESTURE_TYPE_PINCH:
                            // 处理捏合缩放手势
                            log_message(COMPOSITOR_LOG_DEBUG, "Pinch gesture detected, scale: %.2f", event->gesture_scale);
                            
                            // 实现窗口缩放
                            if (g_compositor_state->config.enable_gesture_window_manipulation) {
                                int current_width, current_height, new_width, new_height;
                                int window_x, window_y;
                                
                                if (is_wayland) {
                                    WaylandWindow* window = (WaylandWindow*)surface;
                                    current_width = window->width;
                                    current_height = window->height;
                                    window_x = window->x;
                                    window_y = window->y;
                                } else {
                                    XwaylandWindowState* window = (XwaylandWindowState*)surface;
                                    current_width = window->width;
                                    current_height = window->height;
                                    window_x = window->x;
                                    window_y = window->y;
                                }
                                
                                // 计算新尺寸
                                new_width = (int)(current_width * event->gesture_scale);
                                new_height = (int)(current_height * event->gesture_scale);
                                
                                // 确保最小尺寸
                                if (new_width < MIN_WINDOW_WIDTH) new_width = MIN_WINDOW_WIDTH;
                                if (new_height < MIN_WINDOW_HEIGHT) new_height = MIN_WINDOW_HEIGHT;
                                
                                // 调整窗口位置，使缩放中心点保持不变
                                int delta_width = new_width - current_width;
                                int delta_height = new_height - current_height;
                                
                                int new_x = window_x - delta_width / 2;
                                int new_y = window_y - delta_height / 2;
                                
                                // 边界检查
                                if (new_x < 0) new_x = 0;
                                if (new_y < 0) new_y = 0;
                                
                                // 应用新尺寸和位置
                                if (is_wayland) {
                                    WaylandWindow* window = (WaylandWindow*)surface;
                                    window->x = new_x;
                                    window->y = new_y;
                                    window->width = new_width;
                                    window->height = new_height;
                                } else {
                                    XwaylandWindowState* window = (XwaylandWindowState*)surface;
                                    window->x = new_x;
                                    window->y = new_y;
                                    window->width = new_width;
                                    window->height = new_height;
                                }
                                
                                compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
                            }
                            break;
                            
                        case COMPOSITOR_GESTURE_TYPE_ROTATE:
                            // 处理旋转手势
                            log_message(COMPOSITOR_LOG_DEBUG, "Rotate gesture detected, angle: %.2f", event->gesture_rotation);
                            
                            // 仅在启用旋转功能时处理
                            if (g_compositor_state->config.enable_window_rotation) {
                                // 存储旋转角度到窗口状态
                                if (is_wayland) {
                                    WaylandWindow* window = (WaylandWindow*)surface;
                                    window->rotation = fmod(window->rotation + event->gesture_rotation, 360.0f);
                                } else {
                                    XwaylandWindowState* window = (XwaylandWindowState*)surface;
                                    window->rotation = fmod(window->rotation + event->gesture_rotation, 360.0f);
                                }
                                
                                compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
                            }
                            break;
                            
                        case COMPOSITOR_GESTURE_TYPE_SWIPE:
                            // 处理滑动手势
                            log_message(COMPOSITOR_LOG_DEBUG, "Swipe gesture detected, direction: %d, fingers: %d", 
                                       event->scroll_direction, event->touch_count);
                            
                            // 两指滑动用于工作区切换
                            if (event->touch_count == 2) {
                                if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_LEFT) {
                                    // 切换到下一个工作区
                                    compositor_switch_workspace((g_compositor_state->active_workspace + 1) % g_compositor_state->workspace_count);
                                } else if (event->scroll_direction == COMPOSITOR_SCROLL_DIRECTION_RIGHT) {
                                    // 切换到上一个工作区
                                    compositor_switch_workspace((g_compositor_state->active_workspace - 1 + g_compositor_state->workspace_count) % g_compositor_state->workspace_count);
                                }
                            }
                            // 单指滑动用于窗口移动
                            else if (event->touch_count == 1 && g_compositor_state->config.enable_gesture_window_manipulation) {
                                // 实现窗口拖拽逻辑
                                if (g_gesture_state.is_active && g_gesture_state.type == COMPOSITOR_GESTURE_TYPE_DRAG) {
                                    // 这里应该有窗口拖动实现
                                }
                            }
                            break;
                            
                        case COMPOSITOR_GESTURE_TYPE_TAP:
                            // 处理点击手势
                            log_message(COMPOSITOR_LOG_DEBUG, "Tap gesture detected, fingers: %d", event->touch_count);
                            
                            // 双击最大化/恢复窗口
                            if (event->touch_count == 1 && g_gesture_state.click_count == 2) {
                                if (g_compositor_state->active_window) {
                                    if (g_compositor_state->active_window_is_wayland) {
                                        WaylandWindow* window = (WaylandWindow*)g_compositor_state->active_window;
                                        if (window->state == WINDOW_STATE_MAXIMIZED) {
                                            wayland_window_restore(window);
                                        } else {
                                            wayland_window_maximize(window);
                                        }
                                    } else {
                                        XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state->active_window;
                                        if (window->state == WINDOW_STATE_MAXIMIZED) {
                                            xwayland_window_restore(window);
                                        } else {
                                            xwayland_window_maximize(window);
                                        }
                                    }
                                }
                            }
                            break;
                            
                        default:
                            log_message(COMPOSITOR_LOG_WARN, "Unknown gesture type: %d", event->gesture_type);
                            break;
                    }
                }
            }
            break;
            
        case COMPOSITOR_INPUT_EVENT_TRACKBALL:
            // 新增轨迹球事件支持
            handle_trackball_event(event);
            break;
            
        case COMPOSITOR_INPUT_EVENT_TOUCHPAD:
            // 新增触控板事件支持
            g_input_device_config.device_type_supported[COMPOSITOR_DEVICE_TYPE_TOUCHPAD] = true;
            
            // 转发到相应的处理函数
            if (event->subtype == COMPOSITOR_INPUT_SUBTYPE_GESTURE) {
                // 触控板手势事件
                event->device_type = COMPOSITOR_DEVICE_TYPE_TOUCHPAD;
                compositor_handle_input_event(event);
            } else {
                // 触控板作为鼠标输入处理
                if (event->state == COMPOSITOR_INPUT_STATE_MOTION) {
                    CompositorInputEvent mouse_event;
                    memset(&mouse_event, 0, sizeof(CompositorInputEvent));
                    mouse_event.type = COMPOSITOR_INPUT_EVENT_MOUSE_MOTION;
                    mouse_event.x = event->x;
                    mouse_event.y = event->y;
                    mouse_event.state = COMPOSITOR_INPUT_STATE_MOTION;
                    mouse_event.device_id = event->device_id;
                    compositor_handle_input_event(&mouse_event);
                }
            }
            break;
            
        default:
            log_message(COMPOSITOR_LOG_WARN, "Unknown input event type: %d", event->type);
            break;
    }
    
    return COMPOSITOR_OK;
}

// 处理输入事件（兼容旧接口）
void compositor_handle_input(int type, int x, int y, int key, int state) {
    if (!g_compositor_state) {
        log_message(COMPOSITOR_LOG_ERROR, "Compositor not initialized, cannot handle input");
        return;
    }
    
    // 记录输入事件（调试用）
    if (g_compositor_state->config.debug_mode) {
        log_message(COMPOSITOR_LOG_DEBUG, "Input event (legacy): type=%d, x=%d, y=%d, key=%d, state=%d", 
                   type, x, y, key, state);
    }
    
    // 将旧的输入事件映射到新的事件结构
    CompositorInputEvent event;
    memset(&event, 0, sizeof(CompositorInputEvent));
    event.device_id = -1;  // 默认设备
    
    switch (type) {
        case COMPOSITOR_INPUT_MOTION:
            event.type = COMPOSITOR_INPUT_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            event.state = COMPOSITOR_INPUT_STATE_MOTION;
            break;
            
        case COMPOSITOR_INPUT_BUTTON:
            event.type = COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON;
            event.x = x;
            event.y = y;
            event.button = key;
            event.state = (state == COMPOSITOR_INPUT_STATE_DOWN) ? COMPOSITOR_INPUT_STATE_PRESSED : COMPOSITOR_INPUT_STATE_RELEASED;
            break;
            
        case COMPOSITOR_INPUT_KEY:
            event.type = COMPOSITOR_INPUT_EVENT_KEYBOARD;
            event.keycode = key;
            event.state = (state == COMPOSITOR_INPUT_STATE_DOWN) ? COMPOSITOR_INPUT_STATE_PRESSED : COMPOSITOR_INPUT_STATE_RELEASED;
            break;
            
        case COMPOSITOR_INPUT_TOUCH:
            event.type = COMPOSITOR_INPUT_EVENT_TOUCH;
            event.x = x;
            event.y = y;
            event.state = (state == COMPOSITOR_INPUT_STATE_DOWN) ? COMPOSITOR_INPUT_STATE_PRESSED : 
                          (state == COMPOSITOR_INPUT_STATE_UP) ? COMPOSITOR_INPUT_STATE_RELEASED : COMPOSITOR_INPUT_STATE_MOTION;
            event.touch_count = 1;
            event.touches[0].x = x;
            event.touches[0].y = y;
            break;
            
        default:
            log_message(COMPOSITOR_LOG_WARN, "Unknown legacy input event type: %d", type);
            return;
    }
    
    // 使用新的处理函数处理事件
    compositor_handle_input_event(&event);
}

// 设置输入状态指针
void compositor_input_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 清理输入资源
void compositor_input_cleanup(void) {
    // 释放输入设备资源
    for (int i = 0; i < g_input_device_count; i++) {
        if (g_input_devices[i].name) {
            free((void*)g_input_devices[i].name);
        }
    }
    
    if (g_input_devices) {
        free(g_input_devices);
        g_input_devices = NULL;
    }
    
    // 清理窗口列表资源
    cleanup_window_list();
    
    // 重置状态
    g_input_device_count = 0;
    g_input_device_capacity = 0;
    g_compositor_state = NULL;
    g_active_device = NULL;
    g_alt_key_pressed = false;
    g_window_switching = false;
    
    // 重置手势状态
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
}

// 获取输入设备列表
int compositor_input_get_devices(CompositorInputDevice** out_devices, int* out_count) {
    if (!out_devices || !out_count) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    *out_devices = g_input_devices;
    *out_count = g_input_device_count;
    
    return COMPOSITOR_OK;
}

// 设置活动输入设备
void compositor_input_set_active_device(int device_id) {
    g_active_device = compositor_input_get_device(device_id);
}

// 获取活动输入设备
CompositorInputDevice* compositor_input_get_active_device(void) {
    return g_active_device;
}

// 模拟输入事件
int compositor_input_simulate_event(CompositorInputEventType type, int x, int y, int state) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    CompositorInputEvent event;
    memset(&event, 0, sizeof(CompositorInputEvent));
    event.type = type;
    event.x = x;
    event.y = y;
    event.state = state;
    event.device_id = -1;  // 模拟事件
    
    return compositor_handle_input_event(&event);
}

// 模拟鼠标按钮事件
static void simulate_mouse_button(int x, int y, int button, int state) {
    if (!g_compositor_state) {
        log_message(COMPOSITOR_LOG_ERROR, "Compositor not initialized, cannot simulate mouse button");
        return;
    }
    
    CompositorInputEvent event;
    memset(&event, 0, sizeof(CompositorInputEvent));
    event.type = COMPOSITOR_INPUT_EVENT_MOUSE_BUTTON;
    event.x = x;
    event.y = y;
    event.button = button;
    event.state = state;
    event.device_id = -1;  // 模拟事件
    
    // 更新鼠标位置
    g_compositor_state->mouse_x = x;
    g_compositor_state->mouse_y = y;
    
    compositor_handle_input_event(&event);
}

// 模拟鼠标移动事件
static void simulate_mouse_motion(int x, int y) {
    if (!g_compositor_state) {
        log_message(COMPOSITOR_LOG_ERROR, "Compositor not initialized, cannot simulate mouse motion");
        return;
    }
    
    CompositorInputEvent event;
    memset(&event, 0, sizeof(CompositorInputEvent));
    event.type = COMPOSITOR_INPUT_EVENT_MOUSE_MOTION;
    event.x = x;
    event.y = y;
    event.state = COMPOSITOR_INPUT_STATE_MOTION;
    event.device_id = -1;  // 模拟事件
    
    // 更新鼠标位置
    g_compositor_state->mouse_x = x;
    g_compositor_state->mouse_y = y;
    
    compositor_handle_input_event(&event);
}

// 模拟键盘按键事件
static void simulate_keyboard_key(int keycode, int state) {
    if (!g_compositor_state) {
        log_message(COMPOSITOR_LOG_ERROR, "Compositor not initialized, cannot simulate keyboard key");
        return;
    }
    
    CompositorInputEvent event;
    memset(&event, 0, sizeof(CompositorInputEvent));
    event.type = COMPOSITOR_INPUT_EVENT_KEYBOARD;
    event.keycode = keycode;
    event.state = state;
    event.device_id = -1;  // 模拟事件
    
    compositor_handle_input_event(&event);
}

// 触控笔事件处理函数
static void handle_pen_event(CompositorInputEvent* event) {
    // 确保触控笔设备配置已正确设置
    g_input_device_config.device_type_supported[COMPOSITOR_DEVICE_TYPE_PEN] = true;
    
    // 验证坐标有效性
    if (event->x < 0 || event->x >= g_compositor_state->width ||
        event->y < 0 || event->y >= g_compositor_state->height) {
        log_message(COMPOSITOR_LOG_WARNING, "Pen event with invalid coordinates: (%d,%d)", event->x, event->y);
        return;
    }
    
    // 处理不同的触控笔状态
    switch (event->state) {
        case COMPOSITOR_INPUT_STATE_PRESSED:
            // 记录笔按下位置和时间
            g_compositor_state->pen_last_x = event->x;
            g_compositor_state->pen_last_y = event->y;
            g_compositor_state->pen_pressed_time = get_current_time_ms();
            g_compositor_state->pen_is_pressed = true;
            
            // 处理压力感应
            if (g_compositor_state->config.enable_pen_pressure) {
                g_compositor_state->pen_last_pressure = event->pen_pressure;
                log_message(COMPOSITOR_LOG_DEBUG, "Pen pressed with pressure: %.2f", event->pen_pressure);
            }
            
            // 处理倾斜
            if (g_compositor_state->config.enable_pen_tilt) {
                g_compositor_state->pen_last_tilt_x = event->pen_tilt_x;
                g_compositor_state->pen_last_tilt_y = event->pen_tilt_y;
                log_message(COMPOSITOR_LOG_DEBUG, "Pen tilt: x=%d, y=%d", event->pen_tilt_x, event->pen_tilt_y);
            }
            
            // 模拟鼠标按下，同时保留笔特定属性
            simulate_mouse_button(event->x, event->y, 1, COMPOSITOR_INPUT_STATE_PRESSED);
            break;
            
        case COMPOSITOR_INPUT_STATE_RELEASED:
            // 重置笔状态
            g_compositor_state->pen_is_pressed = false;
            
            // 计算按下持续时间，检测快速点击
            int64_t press_duration = get_current_time_ms() - g_compositor_state->pen_pressed_time;
            if (press_duration < 100) { // 小于100ms认为是快速点击
                log_message(COMPOSITOR_LOG_DEBUG, "Quick pen tap detected: %lldms", press_duration);
            }
            
            // 模拟鼠标释放
            simulate_mouse_button(event->x, event->y, 1, COMPOSITOR_INPUT_STATE_RELEASED);
            break;
            
        case COMPOSITOR_INPUT_STATE_MOTION:
            // 计算移动距离
            int dx = event->x - g_compositor_state->pen_last_x;
            int dy = event->y - g_compositor_state->pen_last_y;
            
            // 更新最后位置
            g_compositor_state->pen_last_x = event->x;
            g_compositor_state->pen_last_y = event->y;
            
            // 处理压力变化
            if (g_compositor_state->config.enable_pen_pressure && 
                fabs(event->pen_pressure - g_compositor_state->pen_last_pressure) > 0.01f) {
                log_message(COMPOSITOR_LOG_DEBUG, "Pen pressure changed: %.2f -> %.2f", 
                           g_compositor_state->pen_last_pressure, event->pen_pressure);
                g_compositor_state->pen_last_pressure = event->pen_pressure;
            }
            
            // 处理倾斜变化
            if (g_compositor_state->config.enable_pen_tilt &&
                (abs(event->pen_tilt_x - g_compositor_state->pen_last_tilt_x) > 5 ||
                 abs(event->pen_tilt_y - g_compositor_state->pen_last_tilt_y) > 5)) {
                log_message(COMPOSITOR_LOG_DEBUG, "Pen tilt changed: x=%d->%d, y=%d->%d", 
                           g_compositor_state->pen_last_tilt_x, event->pen_tilt_x,
                           g_compositor_state->pen_last_tilt_y, event->pen_tilt_y);
                g_compositor_state->pen_last_tilt_x = event->pen_tilt_x;
                g_compositor_state->pen_last_tilt_y = event->pen_tilt_y;
            }
            
            // 模拟鼠标移动
            simulate_mouse_motion(event->x, event->y);
            break;
            
        default:
            log_message(COMPOSITOR_LOG_WARNING, "Unknown pen event state: %d", event->state);
            break;
    }
}

// 游戏控制器事件处理函数
static void handle_gamepad_event(CompositorInputEvent* event) {
    // 确保游戏控制器配置已正确设置
    g_input_device_config.device_type_supported[COMPOSITOR_DEVICE_TYPE_GAMEPAD] = true;
    
    // 处理游戏控制器按钮事件
    if (event->type == COMPOSITOR_INPUT_EVENT_GAMEPAD) {
        // 获取设备配置
        CompositorInputDevice* device = compositor_input_get_device(event->device_id);
        if (!device) {
            log_message(COMPOSITOR_LOG_WARNING, "Gamepad event from unknown device: %d", event->device_id);
            return;
        }
        
        // 处理按钮状态
        if (event->state == COMPOSITOR_INPUT_STATE_PRESSED) {
            // 检查按钮是否已按下，避免重复处理
            if (!device->gamepad_buttons[event->button]) {
                device->gamepad_buttons[event->button] = true;
                log_message(COMPOSITOR_LOG_DEBUG, "Gamepad device %d button %d pressed", 
                           event->device_id, event->button);
                
                // 处理按钮映射
                handle_gamepad_button_mapping(device, event->button, true);
            }
        } else if (event->state == COMPOSITOR_INPUT_STATE_RELEASED) {
            // 检查按钮是否已释放
            if (device->gamepad_buttons[event->button]) {
                device->gamepad_buttons[event->button] = false;
                log_message(COMPOSITOR_LOG_DEBUG, "Gamepad device %d button %d released", 
                           event->device_id, event->button);
                
                // 处理按钮映射释放
                handle_gamepad_button_mapping(device, event->button, false);
            }
        }
    }
    
    // 处理摇杆事件
    if (event->type == COMPOSITOR_INPUT_EVENT_JOYSTICK || event->type == COMPOSITOR_INPUT_EVENT_GAMEPAD) {
        // 检查是否需要模拟鼠标移动
        if (g_compositor_state->config.joystick_mouse_emulation) {
            // 应用死区过滤
            float deadzone = g_compositor_state->config.joystick_deadzone;
            float axis_x = (fabs(event->joystick_axis_x) > deadzone) ? event->joystick_axis_x : 0.0f;
            float axis_y = (fabs(event->joystick_axis_y) > deadzone) ? event->joystick_axis_y : 0.0f;
            
            // 应用灵敏度和非线性曲线
            float sensitivity = g_compositor_state->config.joystick_sensitivity;
            
            // 应用平方曲线以提供更精确的低范围控制
            if (axis_x != 0.0f) {
                axis_x = (axis_x > 0.0f) ? 
                         fminf(axis_x * axis_x * sensitivity, 1.0f) : 
                         fmaxf(-axis_x * axis_x * sensitivity, -1.0f);
            }
            
            if (axis_y != 0.0f) {
                axis_y = (axis_y > 0.0f) ? 
                         fminf(axis_y * axis_y * sensitivity, 1.0f) : 
                         fmaxf(-axis_y * axis_y * sensitivity, -1.0f);
            }
            
            // 计算鼠标移动增量
            int dx = (int)(axis_x * g_compositor_state->config.joystick_max_speed);
            int dy = (int)(axis_y * g_compositor_state->config.joystick_max_speed);
            
            // 如果有有效移动，模拟鼠标移动
            if (dx != 0 || dy != 0) {
                int new_x = g_compositor_state->mouse_x + dx;
                int new_y = g_compositor_state->mouse_y + dy;
                
                // 边界检查
                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                if (new_x >= g_compositor_state->width) new_x = g_compositor_state->width - 1;
                if (new_y >= g_compositor_state->height) new_y = g_compositor_state->height - 1;
                
                simulate_mouse_motion(new_x, new_y);
            }
        }
    }
}

// 处理游戏控制器按钮映射到键盘或鼠标事件
static void handle_gamepad_button_mapping(CompositorInputDevice* device, int button, bool pressed) {
    // 实现按钮映射逻辑
    switch (button) {
        case 0: // A 按钮
            if (pressed) {
                // 可以映射为鼠标左键
                simulate_mouse_button(g_compositor_state->mouse_x, g_compositor_state->mouse_y, 1, COMPOSITOR_INPUT_STATE_PRESSED);
            } else {
                simulate_mouse_button(g_compositor_state->mouse_x, g_compositor_state->mouse_y, 1, COMPOSITOR_INPUT_STATE_RELEASED);
            }
            break;
            
        case 1: // B 按钮
            if (pressed) {
                // 可以映射为鼠标右键
                simulate_mouse_button(g_compositor_state->mouse_x, g_compositor_state->mouse_y, 3, COMPOSITOR_INPUT_STATE_PRESSED);
            } else {
                simulate_mouse_button(g_compositor_state->mouse_x, g_compositor_state->mouse_y, 3, COMPOSITOR_INPUT_STATE_RELEASED);
            }
            break;
            
        case 2: // X 按钮
            if (pressed) {
                // 可以映射为键盘某个键，如Enter
                simulate_keyboard_key(65293, COMPOSITOR_INPUT_STATE_PRESSED);
            } else {
                simulate_keyboard_key(65293, COMPOSITOR_INPUT_STATE_RELEASED);
            }
            break;
            
        case 3: // Y 按钮
            if (pressed) {
                // 可以映射为键盘某个键，如Escape
                simulate_keyboard_key(65307, COMPOSITOR_INPUT_STATE_PRESSED);
            } else {
                simulate_keyboard_key(65307, COMPOSITOR_INPUT_STATE_RELEASED);
            }
            break;
            
        case 4: // L1 按钮
            if (pressed) {
                // 可以映射为键盘Shift键
                simulate_keyboard_key(65505, COMPOSITOR_INPUT_STATE_PRESSED);
            } else {
                simulate_keyboard_key(65505, COMPOSITOR_INPUT_STATE_RELEASED);
            }
            break;
            
        case 5: // R1 按钮
            if (pressed) {
                // 可以映射为键盘Control键
                simulate_keyboard_key(65507, COMPOSITOR_INPUT_STATE_PRESSED);
            } else {
                simulate_keyboard_key(65507, COMPOSITOR_INPUT_STATE_RELEASED);
            }
            break;
            
        default:
            log_message(COMPOSITOR_LOG_DEBUG, "Unmapped gamepad button: %d", button);
            break;
    }
}

// 设置手势识别器配置
void compositor_input_set_gesture_config(int32_t double_tap_timeout, int32_t long_press_timeout, 
                                        float tap_threshold, float swipe_threshold) {
    g_gesture_recognizer_config.double_tap_timeout = double_tap_timeout;
    g_gesture_recognizer_config.long_press_timeout = long_press_timeout;
    g_gesture_recognizer_config.tap_threshold = tap_threshold;
    g_gesture_recognizer_config.swipe_threshold = swipe_threshold;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Gesture config updated: double_tap=%dms, long_press=%dms, tap_thresh=%.1f, swipe_thresh=%.1f",
               double_tap_timeout, long_press_timeout, tap_threshold, swipe_threshold);
}

// 设置游戏控制器配置
void compositor_input_set_gamepad_config(bool enable_mouse_emulation, float sensitivity, 
                                        float deadzone, int max_speed) {
    if (g_compositor_state) {
        g_compositor_state->config.joystick_mouse_emulation = enable_mouse_emulation;
        g_compositor_state->config.joystick_sensitivity = sensitivity;
        g_compositor_state->config.joystick_deadzone = deadzone;
        g_compositor_state->config.joystick_max_speed = max_speed;
        
        log_message(COMPOSITOR_LOG_DEBUG, "Gamepad config updated: mouse_emulation=%d, sensitivity=%.2f, deadzone=%.2f, max_speed=%d",
                   enable_mouse_emulation, sensitivity, deadzone, max_speed);
    }
}

// 设置触控笔配置
void compositor_input_set_pen_config(bool enable_pressure, bool enable_tilt, float pressure_sensitivity) {
    if (g_compositor_state) {
        g_compositor_state->config.enable_pen_pressure = enable_pressure;
        g_compositor_state->config.enable_pen_tilt = enable_tilt;
        g_compositor_state->config.pen_pressure_sensitivity = pressure_sensitivity;
        
        log_message(COMPOSITOR_LOG_DEBUG, "Pen config updated: pressure=%d, tilt=%d, sensitivity=%.2f",
                   enable_pressure, enable_tilt, pressure_sensitivity);
    }
}

// 获取活动触摸点数量
int compositor_input_get_active_touch_points(void) {
    return g_gesture_state.touch_count;
}

// 获取设备能力
bool compositor_input_is_device_type_supported(CompositorDeviceType device_type) {
    if (device_type < 0 || device_type >= 10) {
        return false;
    }
    return g_input_device_config.device_type_supported[device_type];
}

// 检查触控笔压力感应支持
bool compositor_input_has_pressure_support(void) {
    return g_input_device_config.pressure_sensitivity;
}

// 检查触控笔倾斜支持
bool compositor_input_has_tilt_support(void) {
    return g_input_device_config.tilt_support;
}

// 检查触控笔旋转支持
bool compositor_input_has_rotation_support(void) {
    return g_input_device_config.rotation_support;
}