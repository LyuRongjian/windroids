#ifndef COMPOSITOR_INPUT_H
#define COMPOSITOR_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 输入设备类型
typedef enum {
    INPUT_DEVICE_TYPE_TOUCH = 0,
    INPUT_DEVICE_TYPE_MOUSE,
    INPUT_DEVICE_TYPE_KEYBOARD,
    INPUT_DEVICE_TYPE_GAMEPAD,
    INPUT_DEVICE_TYPE_UNKNOWN
} input_device_type_t;

// 输入事件类型
typedef enum {
    INPUT_EVENT_TYPE_CONNECT = 0,
    INPUT_EVENT_TYPE_DISCONNECT,
    INPUT_EVENT_TYPE_TOUCH_DOWN,
    INPUT_EVENT_TYPE_TOUCH_UP,
    INPUT_EVENT_TYPE_TOUCH_MOVE,
    INPUT_EVENT_TYPE_MOUSE_MOVE,
    INPUT_EVENT_TYPE_MOUSE_BUTTON_DOWN,
    INPUT_EVENT_TYPE_MOUSE_BUTTON_UP,
    INPUT_EVENT_TYPE_MOUSE_SCROLL,
    INPUT_EVENT_TYPE_KEY_DOWN,
    INPUT_EVENT_TYPE_KEY_UP,
    INPUT_EVENT_TYPE_GAMEPAD_BUTTON_DOWN,
    INPUT_EVENT_TYPE_GAMEPAD_BUTTON_UP,
    INPUT_EVENT_TYPE_GAMEPAD_AXIS_MOVE,
    INPUT_EVENT_TYPE_UNKNOWN
} input_event_type_t;

// 鼠标按钮
typedef enum {
    MOUSE_BUTTON_LEFT = 0,
    MOUSE_BUTTON_RIGHT,
    MOUSE_BUTTON_MIDDLE,
    MOUSE_BUTTON_X1,
    MOUSE_BUTTON_X2,
    MOUSE_BUTTON_COUNT
} mouse_button_t;

// 键盘修饰键
typedef enum {
    KEYBOARD_MODIFIER_NONE = 0,
    KEYBOARD_MODIFIER_SHIFT = 1 << 0,
    KEYBOARD_MODIFIER_CTRL = 1 << 1,
    KEYBOARD_MODIFIER_ALT = 1 << 2,
    KEYBOARD_MODIFIER_META = 1 << 3
} keyboard_modifier_t;

// Gamepad按钮
typedef enum {
    GAMEPAD_BUTTON_A = 0,
    GAMEPAD_BUTTON_B,
    GAMEPAD_BUTTON_X,
    GAMEPAD_BUTTON_Y,
    GAMEPAD_BUTTON_L1,
    GAMEPAD_BUTTON_R1,
    GAMEPAD_BUTTON_L2,
    GAMEPAD_BUTTON_R2,
    GAMEPAD_BUTTON_L3,
    GAMEPAD_BUTTON_R3,
    GAMEPAD_BUTTON_SELECT,
    GAMEPAD_BUTTON_START,
    GAMEPAD_BUTTON_DPAD_UP,
    GAMEPAD_BUTTON_DPAD_DOWN,
    GAMEPAD_BUTTON_DPAD_LEFT,
    GAMEPAD_BUTTON_DPAD_RIGHT,
    GAMEPAD_BUTTON_COUNT
} gamepad_button_t;

// Gamepad轴
typedef enum {
    GAMEPAD_AXIS_LEFT_X = 0,
    GAMEPAD_AXIS_LEFT_Y,
    GAMEPAD_AXIS_RIGHT_X,
    GAMEPAD_AXIS_RIGHT_Y,
    GAMEPAD_AXIS_L2,
    GAMEPAD_AXIS_R2,
    GAMEPAD_AXIS_COUNT
} gamepad_axis_t;

// 输入设备信息
typedef struct {
    input_device_type_t type;
    uint32_t id;
    char name[64];
    bool connected;
    void* device_data;  // 设备特定数据
} input_device_info_t;

// 触摸事件数据
typedef struct {
    uint32_t touch_id;
    float x;
    float y;
    float pressure;
} input_touch_data_t;

// 鼠标事件数据
typedef struct {
    float x;
    float y;
    mouse_button_t button;
    float scroll_delta_x;
    float scroll_delta_y;
} input_mouse_data_t;

// 键盘事件数据
typedef struct {
    uint32_t keycode;
    keyboard_modifier_t modifiers;
} input_keyboard_data_t;

// Gamepad事件数据
typedef struct {
    gamepad_button_t button;
    gamepad_axis_t axis;
    float axis_value;
} input_gamepad_data_t;

// 输入事件
typedef struct {
    input_event_type_t type;
    input_device_type_t device_type;
    uint32_t device_id;
    uint64_t timestamp;
    
    union {
        input_touch_data_t touch;
        input_mouse_data_t mouse;
        input_keyboard_data_t keyboard;
        input_gamepad_data_t gamepad;
    } data;
} input_event_t;

// 输入事件处理器函数指针
typedef void (*input_event_handler_t)(const input_event_t* event, void* user_data);

// 输入设备连接状态变化处理器
typedef void (*input_device_change_handler_t)(const input_device_info_t* device, bool connected, void* user_data);

// 输入系统初始化
int compositor_input_init(void);

// 输入系统销毁
void compositor_input_destroy(void);

// 输入系统主循环处理
void compositor_input_step(void);

// 注册输入事件处理器
int compositor_input_register_event_handler(input_event_handler_t handler, void* user_data);

// 注销输入事件处理器
void compositor_input_unregister_event_handler(input_event_handler_t handler);

// 注册设备变化处理器
int compositor_input_register_device_change_handler(input_device_change_handler_t handler, void* user_data);

// 注销设备变化处理器
void compositor_input_unregister_device_change_handler(input_device_change_handler_t handler);

// 注入外部输入事件（用于Android输入系统）
void compositor_input_inject_touch_event(uint32_t touch_id, float x, float y, float pressure, bool down);
void compositor_input_inject_mouse_event(float x, float y, mouse_button_t button, bool down);
void compositor_input_inject_mouse_scroll(float delta_x, float delta_y);
void compositor_input_inject_keyboard_event(uint32_t keycode, keyboard_modifier_t modifiers, bool down);
void compositor_input_inject_gamepad_button_event(uint32_t device_id, gamepad_button_t button, bool down);
void compositor_input_inject_gamepad_axis_event(uint32_t device_id, gamepad_axis_t axis, float value);

// 获取连接的设备数量
int compositor_input_get_device_count(void);

// 获取设备信息
int compositor_input_get_device_info(int index, input_device_info_t* info);

// 检查设备是否连接
bool compositor_input_is_device_connected(uint32_t device_id);

// 设置输入映射配置
int compositor_input_set_mapping_config(const char* config_path);

// 启用/禁用输入冲突解决
void compositor_input_set_conflict_resolution(bool enabled);

// 游戏模式相关函数
void input_set_touch_sensitivity(float sensitivity);
void input_set_prediction_enabled(bool enabled);
void input_set_prediction_time(float time_ms);
float input_get_average_latency(void);
uint32_t input_get_touch_event_count(void);
uint32_t input_get_drag_event_count(void);
uint32_t input_get_tap_event_count(void);
uint32_t input_get_predicted_input_count(void);
uint32_t input_get_accurate_prediction_count(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_INPUT_H