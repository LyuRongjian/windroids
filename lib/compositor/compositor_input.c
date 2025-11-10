#include "compositor_input.h"
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define LOG_TAG "CompositorInput"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

#define MAX_DEVICES 16
#define MAX_EVENT_HANDLERS 8
#define MAX_DEVICE_CHANGE_HANDLERS 4
#define MAX_TOUCH_POINTS 10
#define GAMEPAD_AXIS_THRESHOLD 0.1f
#define GAMEPAD_DEADZONE 0.2f
#define MAX_BATCHED_EVENTS 64  // 增加批处理队列大小
#define EVENT_BATCH_TIMEOUT_US 500  // 减少超时时间，提高响应性
#define HIGH_PRIORITY_TIMEOUT_US 100  // 高优先级事件的超时时间

// 事件优先级枚举
typedef enum {
    INPUT_EVENT_PRIORITY_LOW = 0,      // 低优先级：鼠标移动等
    INPUT_EVENT_PRIORITY_NORMAL = 1,   // 普通优先级：触摸移动等
    INPUT_EVENT_PRIORITY_HIGH = 2,     // 高优先级：按钮按下、键盘按键等
    INPUT_EVENT_PRIORITY_CRITICAL = 3  // 关键优先级：系统事件等
} input_event_priority_t;

// 事件批处理项
struct input_event_batch_item {
    struct input_event event;
    uint64_t timestamp;
    input_event_priority_t priority;
};

// 输入系统状态
struct input_system {
    bool initialized;
    
    // 设备管理
    input_device_info_t devices[MAX_DEVICES];
    int device_count;
    
    // 事件处理器
    input_event_handler_t event_handlers[MAX_EVENT_HANDLERS];
    void* event_handler_user_data[MAX_EVENT_HANDLERS];
    int event_handler_count;
    
    input_device_change_handler_t device_change_handlers[MAX_DEVICE_CHANGE_HANDLERS];
    void* device_change_handler_user_data[MAX_DEVICE_CHANGE_HANDLERS];
    int device_change_handler_count;
    
    // 输入映射和冲突解决
    bool conflict_resolution_enabled;
    
    // 触摸状态
    struct {
        bool active;
        uint32_t touch_id;
        float x;
        float y;
        float pressure;
    } touches[MAX_TOUCH_POINTS];
    
    // 鼠标状态
    struct {
        float x;
        float y;
        bool button_pressed[MOUSE_BUTTON_COUNT];
    } mouse;
    
    // 键盘状态
    struct {
        bool keys[256];  // 简化：假设最多256个键码
        keyboard_modifier_t modifiers;
    } keyboard;
    
    // Gamepad状态
    struct {
        bool connected;
        bool buttons[GAMEPAD_BUTTON_COUNT];
        float axes[GAMEPAD_AXIS_COUNT];
    } gamepads[MAX_DEVICES];
    
    // 线程安全
    pthread_mutex_t mutex;
    
    // 事件批处理
    struct input_event_batch_item event_batch[MAX_BATCHED_EVENTS];
    int batch_count;
    uint64_t last_batch_time;
    uint64_t last_high_priority_time;
    bool has_high_priority_events;
    
    // 游戏模式相关状态
    struct {
        float touch_sensitivity;
        bool prediction_enabled;
        float prediction_time_ms;
        uint32_t touch_event_count;
        uint32_t drag_event_count;
        uint32_t tap_event_count;
        uint32_t predicted_input_count;
        uint32_t accurate_prediction_count;
        uint64_t total_input_latency;
        uint32_t input_latency_samples;
        uint64_t last_input_time;
        float last_input_x;
        float last_input_y;
    } game_mode;
};

static struct input_system g_input = {0};

// 内部函数声明
static uint64_t get_timestamp_ms(void);
static int find_device_by_id(uint32_t device_id);
static int find_free_device_slot(void);
static int find_touch_slot(uint32_t touch_id);
static int find_free_touch_slot(void);
static void dispatch_event(const input_event_t* event);
static void dispatch_device_change(const input_device_info_t* device, bool connected);
static void apply_input_mapping(input_event_t* event);
static bool resolve_input_conflicts(const input_event_t* event);
static void handle_touch_event(const input_event_t* event);
static void handle_mouse_event(const input_event_t* event);
static void handle_keyboard_event(const input_event_t* event);
static void handle_gamepad_event(const input_event_t* event);
static void input_flush_event_batch(void);
static void input_add_event_to_batch(struct input_event* event);
static uint64_t input_get_time(void);
static input_event_priority_t get_event_priority(const input_event_t* event);

// 初始化输入系统
int compositor_input_init(void) {
    if (g_input.initialized) {
        LOGE("Input system already initialized");
        return -1;
    }
    
    memset(&g_input, 0, sizeof(g_input));
    
    // 初始化互斥锁
    if (pthread_mutex_init(&g_input.mutex, NULL) != 0) {
        LOGE("Failed to initialize mutex");
        return -1;
    }
    
    // 初始化事件批处理
    g_input.batch_count = 0;
    g_input.last_batch_time = input_get_time();
    g_input.last_high_priority_time = input_get_time();
    g_input.has_high_priority_events = false;
    
    // 初始化游戏模式相关状态
    g_input.game_mode.touch_sensitivity = 1.0f;
    g_input.game_mode.prediction_enabled = false;
    g_input.game_mode.prediction_time_ms = 8.0f;
    g_input.game_mode.touch_event_count = 0;
    g_input.game_mode.drag_event_count = 0;
    g_input.game_mode.tap_event_count = 0;
    g_input.game_mode.predicted_input_count = 0;
    g_input.game_mode.accurate_prediction_count = 0;
    g_input.game_mode.total_input_latency = 0;
    g_input.game_mode.input_latency_samples = 0;
    g_input.game_mode.last_input_time = 0;
    g_input.game_mode.last_input_x = 0.0f;
    g_input.game_mode.last_input_y = 0.0f;
    
    // 默认启用冲突解决
    g_input.conflict_resolution_enabled = true;
    
    g_input.initialized = true;
    LOGI("Input system initialized");
    return 0;
}

// 销毁输入系统
void compositor_input_destroy(void) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_destroy(&g_input.mutex);
    memset(&g_input, 0, sizeof(g_input));
    
    LOGI("Input system destroyed");
}

// 输入系统主循环处理
void compositor_input_step(void) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    // 检查是否需要刷新事件批处理队列
    uint64_t current_time = input_get_time();
    
    // 根据是否有高优先级事件决定使用哪个超时时间
    uint64_t timeout = g_input.has_high_priority_events ? 
                       HIGH_PRIORITY_TIMEOUT_US : EVENT_BATCH_TIMEOUT_US;
    
    if (g_input.batch_count > 0 && 
        (current_time - g_input.last_batch_time) >= timeout) {
        input_flush_event_batch();
    }
    
    // 如果有高优先级事件且超过了高优先级超时时间，强制刷新
    if (g_input.has_high_priority_events && 
        (current_time - g_input.last_high_priority_time) >= HIGH_PRIORITY_TIMEOUT_US) {
        input_flush_event_batch();
    }
    
    // 这里可以添加定期检查设备连接状态的逻辑
    // 例如，轮询游戏手柄连接状态
    
    pthread_mutex_unlock(&g_input.mutex);
}

// 注册输入事件处理器
int compositor_input_register_event_handler(input_event_handler_t handler, void* user_data) {
    if (!g_input.initialized || !handler) {
        return -1;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    if (g_input.event_handler_count >= MAX_EVENT_HANDLERS) {
        pthread_mutex_unlock(&g_input.mutex);
        LOGE("Maximum event handlers reached");
        return -1;
    }
    
    g_input.event_handlers[g_input.event_handler_count] = handler;
    g_input.event_handler_user_data[g_input.event_handler_count] = user_data;
    g_input.event_handler_count++;
    
    pthread_mutex_unlock(&g_input.mutex);
    
    return 0;
}

// 注销输入事件处理器
void compositor_input_unregister_event_handler(input_event_handler_t handler) {
    if (!g_input.initialized || !handler) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    for (int i = 0; i < g_input.event_handler_count; i++) {
        if (g_input.event_handlers[i] == handler) {
            // 移动后面的处理器向前
            for (int j = i; j < g_input.event_handler_count - 1; j++) {
                g_input.event_handlers[j] = g_input.event_handlers[j + 1];
                g_input.event_handler_user_data[j] = g_input.event_handler_user_data[j + 1];
            }
            g_input.event_handler_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_input.mutex);
}

// 注册设备变化处理器
int compositor_input_register_device_change_handler(input_device_change_handler_t handler, void* user_data) {
    if (!g_input.initialized || !handler) {
        return -1;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    if (g_input.device_change_handler_count >= MAX_DEVICE_CHANGE_HANDLERS) {
        pthread_mutex_unlock(&g_input.mutex);
        LOGE("Maximum device change handlers reached");
        return -1;
    }
    
    g_input.device_change_handlers[g_input.device_change_handler_count] = handler;
    g_input.device_change_handler_user_data[g_input.device_change_handler_count] = user_data;
    g_input.device_change_handler_count++;
    
    pthread_mutex_unlock(&g_input.mutex);
    
    return 0;
}

// 注销设备变化处理器
void compositor_input_unregister_device_change_handler(input_device_change_handler_t handler) {
    if (!g_input.initialized || !handler) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    for (int i = 0; i < g_input.device_change_handler_count; i++) {
        if (g_input.device_change_handlers[i] == handler) {
            // 移动后面的处理器向前
            for (int j = i; j < g_input.device_change_handler_count - 1; j++) {
                g_input.device_change_handlers[j] = g_input.device_change_handlers[j + 1];
                g_input.device_change_handler_user_data[j] = g_input.device_change_handler_user_data[j + 1];
            }
            g_input.device_change_handler_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_input.mutex);
}

// 注入触摸事件
void compositor_input_inject_touch_event(uint32_t touch_id, float x, float y, float pressure, bool down) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    input_event_t event = {
        .type = down ? INPUT_EVENT_TYPE_TOUCH_DOWN : INPUT_EVENT_TYPE_TOUCH_UP,
        .device_type = INPUT_DEVICE_TYPE_TOUCH,
        .device_id = 0,  // 触摸屏通常只有一个设备
        .timestamp = get_timestamp_ms(),
        .data.touch = {
            .touch_id = touch_id,
            .x = x,
            .y = y,
            .pressure = pressure
        }
    };
    
    // 如果是移动事件，需要先查找触摸点
    if (down) {
        int slot = find_touch_slot(touch_id);
        if (slot >= 0) {
            // 已存在的触摸点，可能是移动事件
            event.type = INPUT_EVENT_TYPE_TOUCH_MOVE;
        } else {
            // 新的触摸点
            slot = find_free_touch_slot();
            if (slot < 0) {
                pthread_mutex_unlock(&g_input.mutex);
                LOGE("No free touch slots available");
                return;
            }
            
            g_input.touches[slot].active = true;
            g_input.touches[slot].touch_id = touch_id;
            g_input.touches[slot].x = x;
            g_input.touches[slot].y = y;
            g_input.touches[slot].pressure = pressure;
        }
    } else {
        // 释放触摸点
        int slot = find_touch_slot(touch_id);
        if (slot >= 0) {
            g_input.touches[slot].active = false;
        }
    }
    
    // 应用输入映射
    apply_input_mapping(&event);
    
    // 解决输入冲突
    if (g_input.conflict_resolution_enabled && !resolve_input_conflicts(&event)) {
        pthread_mutex_unlock(&g_input.mutex);
        return;  // 事件被冲突解决机制丢弃
    }
    
    // 分发事件
    dispatch_event(&event);
    
    // 更新游戏模式相关状态
    g_input.game_mode.touch_event_count++;
    g_input.game_mode.last_input_time = get_timestamp_ms();
    g_input.game_mode.last_input_x = x;
    g_input.game_mode.last_input_y = y;
    
    // 计算输入延迟
    if (g_input.game_mode.last_input_time > 0) {
        uint64_t current_time = get_timestamp_ms();
        uint64_t latency = current_time - g_input.game_mode.last_input_time;
        g_input.game_mode.total_input_latency += latency;
        g_input.game_mode.input_latency_samples++;
    }
    
    pthread_mutex_unlock(&g_input.mutex);
}

// 注入鼠标事件
void compositor_input_inject_mouse_event(float x, float y, mouse_button_t button, bool down) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    // 更新鼠标位置
    g_input.mouse.x = x;
    g_input.mouse.y = y;
    
    // 更新按钮状态
    if (button < MOUSE_BUTTON_COUNT) {
        g_input.mouse.button_pressed[button] = down;
    }
    
    input_event_t event = {
        .device_type = INPUT_DEVICE_TYPE_MOUSE,
        .device_id = 0,  // 鼠标通常只有一个设备
        .timestamp = get_timestamp_ms(),
        .data.mouse = {
            .x = x,
            .y = y,
            .button = button
        }
    };
    
    if (down) {
        event.type = INPUT_EVENT_TYPE_MOUSE_BUTTON_DOWN;
    } else {
        event.type = INPUT_EVENT_TYPE_MOUSE_BUTTON_UP;
    }
    
    // 应用输入映射
    apply_input_mapping(&event);
    
    // 解决输入冲突
    if (g_input.conflict_resolution_enabled && !resolve_input_conflicts(&event)) {
        pthread_mutex_unlock(&g_input.mutex);
        return;  // 事件被冲突解决机制丢弃
    }
    
    // 分发事件
    dispatch_event(&event);
    
    // 更新游戏模式相关状态
    g_input.game_mode.last_input_time = get_timestamp_ms();
    g_input.game_mode.last_input_x = x;
    g_input.game_mode.last_input_y = y;
    
    // 计算输入延迟
    if (g_input.game_mode.last_input_time > 0) {
        uint64_t current_time = get_timestamp_ms();
        uint64_t latency = current_time - g_input.game_mode.last_input_time;
        g_input.game_mode.total_input_latency += latency;
        g_input.game_mode.input_latency_samples++;
    }
    
    pthread_mutex_unlock(&g_input.mutex);
}

// 注入鼠标滚轮事件
void compositor_input_inject_mouse_scroll(float delta_x, float delta_y) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    input_event_t event = {
        .type = INPUT_EVENT_TYPE_MOUSE_SCROLL,
        .device_type = INPUT_DEVICE_TYPE_MOUSE,
        .device_id = 0,  // 鼠标通常只有一个设备
        .timestamp = get_timestamp_ms(),
        .data.mouse = {
            .x = g_input.mouse.x,
            .y = g_input.mouse.y,
            .scroll_delta_x = delta_x,
            .scroll_delta_y = delta_y
        }
    };
    
    // 应用输入映射
    apply_input_mapping(&event);
    
    // 解决输入冲突
    if (g_input.conflict_resolution_enabled && !resolve_input_conflicts(&event)) {
        pthread_mutex_unlock(&g_input.mutex);
        return;  // 事件被冲突解决机制丢弃
    }
    
    // 分发事件
    dispatch_event(&event);
    
    // 更新游戏模式相关状态
    g_input.game_mode.last_input_time = get_timestamp_ms();
    
    // 计算输入延迟
    if (g_input.game_mode.last_input_time > 0) {
        uint64_t current_time = get_timestamp_ms();
        uint64_t latency = current_time - g_input.game_mode.last_input_time;
        g_input.game_mode.total_input_latency += latency;
        g_input.game_mode.input_latency_samples++;
    }
    
    pthread_mutex_unlock(&g_input.mutex);
}

// 注入键盘事件
void compositor_input_inject_keyboard_event(uint32_t keycode, keyboard_modifier_t modifiers, bool down) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    // 更新键盘状态
    if (keycode < 256) {
        g_input.keyboard.keys[keycode] = down;
    }
    g_input.keyboard.modifiers = modifiers;
    
    input_event_t event = {
        .device_type = INPUT_DEVICE_TYPE_KEYBOARD,
        .device_id = 0,  // 键盘通常只有一个设备
        .timestamp = get_timestamp_ms(),
        .data.keyboard = {
            .keycode = keycode,
            .modifiers = modifiers
        }
    };
    
    if (down) {
        event.type = INPUT_EVENT_TYPE_KEY_DOWN;
    } else {
        event.type = INPUT_EVENT_TYPE_KEY_UP;
    }
    
    // 应用输入映射
    apply_input_mapping(&event);
    
    // 解决输入冲突
    if (g_input.conflict_resolution_enabled && !resolve_input_conflicts(&event)) {
        pthread_mutex_unlock(&g_input.mutex);
        return;  // 事件被冲突解决机制丢弃
    }
    
    // 分发事件
    dispatch_event(&event);
    
    // 更新游戏模式相关状态
    g_input.game_mode.last_input_time = get_timestamp_ms();
    
    // 计算输入延迟
    if (g_input.game_mode.last_input_time > 0) {
        uint64_t current_time = get_timestamp_ms();
        uint64_t latency = current_time - g_input.game_mode.last_input_time;
        g_input.game_mode.total_input_latency += latency;
        g_input.game_mode.input_latency_samples++;
    }
    
    pthread_mutex_unlock(&g_input.mutex);
}

// 注入Gamepad按钮事件
void compositor_input_inject_gamepad_button_event(uint32_t device_id, gamepad_button_t button, bool down) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    // 查找或创建Gamepad设备
    int device_index = find_device_by_id(device_id);
    if (device_index < 0) {
        // 设备不存在，创建新设备
        device_index = find_free_device_slot();
        if (device_index < 0) {
            pthread_mutex_unlock(&g_input.mutex);
            LOGE("No free device slots available");
            return;
        }
        
        g_input.devices[device_index].type = INPUT_DEVICE_TYPE_GAMEPAD;
        g_input.devices[device_index].id = device_id;
        snprintf(g_input.devices[device_index].name, sizeof(g_input.devices[device_index].name), 
                "Gamepad %d", device_id);
        g_input.devices[device_index].connected = true;
        g_input.device_count++;
        
        // 初始化Gamepad状态
        memset(&g_input.gamepads[device_index], 0, sizeof(g_input.gamepads[device_index]));
        g_input.gamepads[device_index].connected = true;
        
        // 分发设备连接事件
        dispatch_device_change(&g_input.devices[device_index], true);
    }
    
    // 更新Gamepad按钮状态
    if (button < GAMEPAD_BUTTON_COUNT) {
        g_input.gamepads[device_index].buttons[button] = down;
    }
    
    input_event_t event = {
        .type = down ? INPUT_EVENT_TYPE_GAMEPAD_BUTTON_DOWN : INPUT_EVENT_TYPE_GAMEPAD_BUTTON_UP,
        .device_type = INPUT_DEVICE_TYPE_GAMEPAD,
        .device_id = device_id,
        .timestamp = get_timestamp_ms(),
        .data.gamepad = {
            .button = button
        }
    };
    
    // 应用输入映射
    apply_input_mapping(&event);
    
    // 解决输入冲突
    if (g_input.conflict_resolution_enabled && !resolve_input_conflicts(&event)) {
        pthread_mutex_unlock(&g_input.mutex);
        return;  // 事件被冲突解决机制丢弃
    }
    
    // 分发事件
    dispatch_event(&event);
    
    // 更新游戏模式相关状态
    g_input.game_mode.last_input_time = get_timestamp_ms();
    
    // 计算输入延迟
    if (g_input.game_mode.last_input_time > 0) {
        uint64_t current_time = get_timestamp_ms();
        uint64_t latency = current_time - g_input.game_mode.last_input_time;
        g_input.game_mode.total_input_latency += latency;
        g_input.game_mode.input_latency_samples++;
    }
    
    pthread_mutex_unlock(&g_input.mutex);
}

// 注入Gamepad轴事件
void compositor_input_inject_gamepad_axis_event(uint32_t device_id, gamepad_axis_t axis, float value) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    // 查找或创建Gamepad设备
    int device_index = find_device_by_id(device_id);
    if (device_index < 0) {
        // 设备不存在，创建新设备
        device_index = find_free_device_slot();
        if (device_index < 0) {
            pthread_mutex_unlock(&g_input.mutex);
            LOGE("No free device slots available");
            return;
        }
        
        g_input.devices[device_index].type = INPUT_DEVICE_TYPE_GAMEPAD;
        g_input.devices[device_index].id = device_id;
        snprintf(g_input.devices[device_index].name, sizeof(g_input.devices[device_index].name), 
                "Gamepad %d", device_id);
        g_input.devices[device_index].connected = true;
        g_input.device_count++;
        
        // 初始化Gamepad状态
        memset(&g_input.gamepads[device_index], 0, sizeof(g_input.gamepads[device_index]));
        g_input.gamepads[device_index].connected = true;
        
        // 分发设备连接事件
        dispatch_device_change(&g_input.devices[device_index], true);
    }
    
    // 更新Gamepad轴状态
    if (axis < GAMEPAD_AXIS_COUNT) {
        g_input.gamepads[device_index].axes[axis] = value;
    }
    
    input_event_t event = {
        .type = INPUT_EVENT_TYPE_GAMEPAD_AXIS_MOVE,
        .device_type = INPUT_DEVICE_TYPE_GAMEPAD,
        .device_id = device_id,
        .timestamp = get_timestamp_ms(),
        .data.gamepad = {
            .axis = axis,
            .axis_value = value
        }
    };
    
    // 应用输入映射
    apply_input_mapping(&event);
    
    // 解决输入冲突
    if (g_input.conflict_resolution_enabled && !resolve_input_conflicts(&event)) {
        pthread_mutex_unlock(&g_input.mutex);
        return;  // 事件被冲突解决机制丢弃
    }
    
    // 分发事件
    dispatch_event(&event);
    
    pthread_mutex_unlock(&g_input.mutex);
}

// 获取连接的设备数量
int compositor_input_get_device_count(void) {
    if (!g_input.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    int count = g_input.device_count;
    pthread_mutex_unlock(&g_input.mutex);
    
    return count;
}

// 获取设备信息
int compositor_input_get_device_info(int index, input_device_info_t* info) {
    if (!g_input.initialized || !info || index < 0 || index >= g_input.device_count) {
        return -1;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    *info = g_input.devices[index];
    pthread_mutex_unlock(&g_input.mutex);
    
    return 0;
}

// 检查设备是否连接
bool compositor_input_is_device_connected(uint32_t device_id) {
    if (!g_input.initialized) {
        return false;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    
    bool connected = false;
    int device_index = find_device_by_id(device_id);
    if (device_index >= 0) {
        connected = g_input.devices[device_index].connected;
    }
    
    pthread_mutex_unlock(&g_input.mutex);
    
    return connected;
}

// 设置输入映射配置
int compositor_input_set_mapping_config(const char* config_path) {
    if (!g_input.initialized || !config_path) {
        return -1;
    }
    
    // 简化实现：暂时不支持配置文件
    // 实际实现中，应该解析配置文件并应用映射规则
    LOGI("Input mapping configuration not yet implemented");
    return 0;
}

// 启用/禁用输入冲突解决
void compositor_input_set_conflict_resolution(bool enabled) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    g_input.conflict_resolution_enabled = enabled;
    pthread_mutex_unlock(&g_input.mutex);
    
    LOGI("Input conflict resolution %s", enabled ? "enabled" : "disabled");
}

// 内部函数实现

// 获取当前时间戳（毫秒）
static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// 根据设备ID查找设备
static int find_device_by_id(uint32_t device_id) {
    for (int i = 0; i < g_input.device_count; i++) {
        if (g_input.devices[i].id == device_id) {
            return i;
        }
    }
    return -1;
}

// 查找空闲的设备槽位
static int find_free_device_slot(void) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!g_input.devices[i].connected) {
            return i;
        }
    }
    return -1;
}

// 根据触摸ID查找触摸点
static int find_touch_slot(uint32_t touch_id) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (g_input.touches[i].active && g_input.touches[i].touch_id == touch_id) {
            return i;
        }
    }
    return -1;
}

// 查找空闲的触摸点槽位
static int find_free_touch_slot(void) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (!g_input.touches[i].active) {
            return i;
        }
    }
    return -1;
}

// 分发事件
static void dispatch_event(const input_event_t* event) {
    // 添加事件到批处理队列
    input_add_event_to_batch((struct input_event*)event);
    
    // 检查是否需要立即刷新批处理
    uint64_t current_time = input_get_time();
    input_event_priority_t priority = get_event_priority(event);
    
    // 高优先级事件使用更短的超时时间
    uint64_t timeout = (priority >= INPUT_EVENT_PRIORITY_HIGH) ? 
                       HIGH_PRIORITY_TIMEOUT_US : EVENT_BATCH_TIMEOUT_US;
    
    // 如果有高优先级事件，使用高优先级超时
    if (g_input.has_high_priority_events) {
        timeout = HIGH_PRIORITY_TIMEOUT_US;
    }
    
    if (g_input.batch_count >= MAX_BATCHED_EVENTS || 
        (current_time - g_input.last_batch_time) >= timeout ||
        (g_input.has_high_priority_events && 
         (current_time - g_input.last_high_priority_time) >= HIGH_PRIORITY_TIMEOUT_US)) {
        input_flush_event_batch();
    }
}

// 分发设备变化事件
static void dispatch_device_change(const input_device_info_t* device, bool connected) {
    for (int i = 0; i < g_input.device_change_handler_count; i++) {
        if (g_input.device_change_handlers[i]) {
            g_input.device_change_handlers[i](device, connected, g_input.device_change_handler_user_data[i]);
        }
    }
}

// 应用输入映射
static void apply_input_mapping(input_event_t* event) {
    // 简化实现：暂时不进行复杂的输入映射
    // 实际实现中，应该根据配置文件中的映射规则转换输入事件
    // 例如，可以将Gamepad的A按钮映射为鼠标左键点击
    
    // 示例：将Gamepad的DPAD转换为鼠标移动
    if (event->type == INPUT_EVENT_TYPE_GAMEPAD_BUTTON_DOWN || 
        event->type == INPUT_EVENT_TYPE_GAMEPAD_BUTTON_UP) {
        gamepad_button_t button = event->data.gamepad.button;
        
        // 如果是DPAD按钮，转换为鼠标移动事件
        if (button >= GAMEPAD_BUTTON_DPAD_UP && button <= GAMEPAD_BUTTON_DPAD_RIGHT) {
            float delta_x = 0.0f, delta_y = 0.0f;
            
            switch (button) {
                case GAMEPAD_BUTTON_DPAD_UP:
                    delta_y = -10.0f;
                    break;
                case GAMEPAD_BUTTON_DPAD_DOWN:
                    delta_y = 10.0f;
                    break;
                case GAMEPAD_BUTTON_DPAD_LEFT:
                    delta_x = -10.0f;
                    break;
                case GAMEPAD_BUTTON_DPAD_RIGHT:
                    delta_x = 10.0f;
                    break;
                default:
                    break;
            }
            
            // 转换为鼠标移动事件
            event->type = INPUT_EVENT_TYPE_MOUSE_MOVE;
            event->device_type = INPUT_DEVICE_TYPE_MOUSE;
            event->data.mouse.x = g_input.mouse.x + delta_x;
            event->data.mouse.y = g_input.mouse.y + delta_y;
        }
    }
}

// 解决输入冲突
static bool resolve_input_conflicts(const input_event_t* event) {
    // 简化实现：基本的冲突解决策略
    
    // 1. 如果同时有触摸和鼠标输入，优先使用鼠标输入
    if (event->device_type == INPUT_DEVICE_TYPE_TOUCH) {
        // 检查最近是否有鼠标输入
        // 简化：如果鼠标有任何按钮被按下，则忽略触摸输入
        for (int i = 0; i < MOUSE_BUTTON_COUNT; i++) {
            if (g_input.mouse.button_pressed[i]) {
                return false;  // 丢弃触摸事件
            }
        }
    }
    
    // 2. 如果同时有键盘和Gamepad输入，优先使用键盘输入
    if (event->device_type == INPUT_DEVICE_TYPE_GAMEPAD) {
        // 检查最近是否有键盘输入
        // 简化：如果键盘有任何修饰键被按下，则忽略Gamepad输入
        if (g_input.keyboard.modifiers != KEYBOARD_MODIFIER_NONE) {
            return false;  // 丢弃Gamepad事件
        }
    }
    
    // 3. 对于Gamepad轴输入，应用死区
    if (event->type == INPUT_EVENT_TYPE_GAMEPAD_AXIS_MOVE) {
        float value = event->data.gamepad.axis_value;
        if (fabsf(value) < GAMEPAD_DEADZONE) {
            return false;  // 丢弃死区内的轴输入
        }
    }
    
    return true;  // 允许事件通过
}

// 添加事件到批处理队列
static void input_add_event_to_batch(struct input_event* event) {
    input_event_priority_t priority = get_event_priority(event);
    
    // 如果是高优先级事件且批处理队列中有低优先级事件，考虑立即刷新
    if (priority >= INPUT_EVENT_PRIORITY_HIGH && g_input.batch_count > 0) {
        bool has_low_priority = false;
        for (int i = 0; i < g_input.batch_count; i++) {
            if (g_input.event_batch[i].priority < priority) {
                has_low_priority = true;
                break;
            }
        }
        
        if (has_low_priority) {
            input_flush_event_batch();
        }
    }
    
    if (g_input.batch_count >= MAX_BATCHED_EVENTS) {
        // 批处理队列已满，先刷新
        input_flush_event_batch();
    }
    
    // 添加事件到批处理队列
    g_input.event_batch[g_input.batch_count].event = *event;
    g_input.event_batch[g_input.batch_count].timestamp = input_get_time();
    g_input.event_batch[g_input.batch_count].priority = priority;
    g_input.batch_count++;
    
    // 更新高优先级事件标志
    if (priority >= INPUT_EVENT_PRIORITY_HIGH) {
        g_input.has_high_priority_events = true;
        g_input.last_high_priority_time = input_get_time();
    }
}

// 刷新事件批处理队列
static void input_flush_event_batch(void) {
    if (g_input.batch_count == 0) {
        return;
    }
    
    // 按优先级排序事件（简单冒泡排序，因为队列通常很小）
    for (int i = 0; i < g_input.batch_count - 1; i++) {
        for (int j = 0; j < g_input.batch_count - i - 1; j++) {
            if (g_input.event_batch[j].priority < g_input.event_batch[j + 1].priority) {
                // 交换事件
                struct input_event_batch_item temp = g_input.event_batch[j];
                g_input.event_batch[j] = g_input.event_batch[j + 1];
                g_input.event_batch[j + 1] = temp;
            }
        }
    }
    
    // 分发所有批处理的事件
    for (int i = 0; i < g_input.batch_count; i++) {
        for (int j = 0; j < g_input.event_handler_count; j++) {
            if (g_input.event_handlers[j]) {
                g_input.event_handlers[j](&g_input.event_batch[i].event, g_input.event_handler_user_data[j]);
            }
        }
    }
    
    // 重置批处理队列
    g_input.batch_count = 0;
    g_input.last_batch_time = input_get_time();
    g_input.has_high_priority_events = false;
}

// 获取事件优先级
static input_event_priority_t get_event_priority(const input_event_t* event) {
    // 键盘事件通常是高优先级
    if (event->device_type == INPUT_DEVICE_TYPE_KEYBOARD) {
        return INPUT_EVENT_PRIORITY_HIGH;
    }
    
    // 按钮按下/释放事件是高优先级
    if (event->type == INPUT_EVENT_TYPE_MOUSE_BUTTON_DOWN ||
        event->type == INPUT_EVENT_TYPE_MOUSE_BUTTON_UP ||
        event->type == INPUT_EVENT_TYPE_TOUCH_DOWN ||
        event->type == INPUT_EVENT_TYPE_TOUCH_UP ||
        event->type == INPUT_EVENT_TYPE_GAMEPAD_BUTTON_DOWN ||
        event->type == INPUT_EVENT_TYPE_GAMEPAD_BUTTON_UP) {
        return INPUT_EVENT_PRIORITY_HIGH;
    }
    
    // 鼠标移动和触摸移动是普通优先级
    if (event->type == INPUT_EVENT_TYPE_MOUSE_MOVE ||
        event->type == INPUT_EVENT_TYPE_TOUCH_MOVE) {
        return INPUT_EVENT_PRIORITY_NORMAL;
    }
    
    // 游戏手柄轴移动是低优先级
    if (event->type == INPUT_EVENT_TYPE_GAMEPAD_AXIS_MOVE) {
        return INPUT_EVENT_PRIORITY_LOW;
    }
    
    // 默认为普通优先级
    return INPUT_EVENT_PRIORITY_NORMAL;
}

// 获取当前时间戳（微秒）
static uint64_t input_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

// 游戏模式相关函数实现

// 设置触摸灵敏度
void input_set_touch_sensitivity(float sensitivity) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    g_input.game_mode.touch_sensitivity = sensitivity;
    LOGI("Touch sensitivity set to %.2f", sensitivity);
    pthread_mutex_unlock(&g_input.mutex);
}

// 启用/禁用输入预测
void input_set_prediction_enabled(bool enabled) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    g_input.game_mode.prediction_enabled = enabled;
    LOGI("Input prediction %s", enabled ? "enabled" : "disabled");
    pthread_mutex_unlock(&g_input.mutex);
}

// 设置输入预测时间
void input_set_prediction_time(float time_ms) {
    if (!g_input.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    g_input.game_mode.prediction_time_ms = time_ms;
    LOGI("Input prediction time set to %.2f ms", time_ms);
    pthread_mutex_unlock(&g_input.mutex);
}

// 获取平均输入延迟
float input_get_average_latency(void) {
    if (!g_input.initialized) {
        return 0.0f;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    float latency = 0.0f;
    if (g_input.game_mode.input_latency_samples > 0) {
        latency = (float)g_input.game_mode.total_input_latency / g_input.game_mode.input_latency_samples;
    } else {
        latency = 16.0f; // 默认16ms延迟
    }
    pthread_mutex_unlock(&g_input.mutex);
    
    return latency;
}

// 获取触摸事件计数
uint32_t input_get_touch_event_count(void) {
    if (!g_input.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    uint32_t count = g_input.game_mode.touch_event_count;
    pthread_mutex_unlock(&g_input.mutex);
    
    return count;
}

// 获取拖动事件计数
uint32_t input_get_drag_event_count(void) {
    if (!g_input.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    uint32_t count = g_input.game_mode.drag_event_count;
    pthread_mutex_unlock(&g_input.mutex);
    
    return count;
}

// 获取点击事件计数
uint32_t input_get_tap_event_count(void) {
    if (!g_input.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    uint32_t count = g_input.game_mode.tap_event_count;
    pthread_mutex_unlock(&g_input.mutex);
    
    return count;
}

// 获取预测输入计数
uint32_t input_get_predicted_input_count(void) {
    if (!g_input.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    uint32_t count = g_input.game_mode.predicted_input_count;
    pthread_mutex_unlock(&g_input.mutex);
    
    return count;
}

// 获取准确预测计数
uint32_t input_get_accurate_prediction_count(void) {
    if (!g_input.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_input.mutex);
    uint32_t count = g_input.game_mode.accurate_prediction_count;
    pthread_mutex_unlock(&g_input.mutex);
    
    return count;
}