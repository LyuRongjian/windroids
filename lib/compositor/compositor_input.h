#ifndef COMPOSITOR_INPUT_H
#define COMPOSITOR_INPUT_H

#include <stdbool.h>
#include <stdint.h>

// 前向声明
struct CompositorState;
typedef struct CompositorState CompositorState;

// 设备类型
enum {
    COMPOSITOR_DEVICE_TYPE_UNKNOWN = 0,
    COMPOSITOR_DEVICE_TYPE_MOUSE = 1,
    COMPOSITOR_DEVICE_TYPE_KEYBOARD = 2,
    COMPOSITOR_DEVICE_TYPE_TOUCHSCREEN = 3,
    COMPOSITOR_DEVICE_TYPE_PEN = 4,        // 触控笔
    COMPOSITOR_DEVICE_TYPE_TOUCHPAD = 5,   // 触摸板
    COMPOSITOR_DEVICE_TYPE_JOYSTICK = 6,   // 游戏手柄
    COMPOSITOR_DEVICE_TYPE_GAMEPAD = 7,    // 游戏控制器
    COMPOSITOR_DEVICE_TYPE_REMOTE = 8,     // 遥控器
    COMPOSITOR_DEVICE_TYPE_TRACKBALL = 9   // 轨迹球
};

// 输入事件类型
enum {
    COMPOSITOR_INPUT_NONE = 0,
    COMPOSITOR_INPUT_MOTION = 1,           // 鼠标/触摸移动
    COMPOSITOR_INPUT_BUTTON = 2,           // 按钮点击
    COMPOSITOR_INPUT_KEY = 3,              // 键盘按键
    COMPOSITOR_INPUT_TOUCH = 4,            // 触摸事件
    COMPOSITOR_INPUT_PEN = 5,              // 触控笔事件
    COMPOSITOR_INPUT_JOYSTICK_AXIS = 6,    // 摇杆轴
    COMPOSITOR_INPUT_JOYSTICK_BUTTON = 7,  // 摇杆按钮
    COMPOSITOR_INPUT_SCROLL = 8,           // 滚动事件
    COMPOSITOR_INPUT_GESTURE = 9,          // 手势事件
    COMPOSITOR_INPUT_DRAG = 10             // 拖拽事件
};

// 输入状态
enum {
    COMPOSITOR_INPUT_STATE_UP = 0,
    COMPOSITOR_INPUT_STATE_DOWN = 1,
    COMPOSITOR_INPUT_STATE_MOVE = 2
};

// 手势类型
enum {
    COMPOSITOR_GESTURE_NONE = 0,
    COMPOSITOR_GESTURE_PINCH = 1,          // 捏合手势（缩放）
    COMPOSITOR_GESTURE_ROTATE = 2,         // 旋转手势
    COMPOSITOR_GESTURE_SWIPE = 3,          // 滑动手势
    COMPOSITOR_GESTURE_TWO_FINGER_TAP = 4  // 双指点击
};

// 输入设备信息
typedef struct {
    int32_t device_id;
    int32_t device_type;
    const char* device_name;
    bool has_motion;
    bool has_buttons;
    bool has_touch;
    bool has_pressure;
    int32_t max_touches;
    int32_t axis_count;
    int32_t button_count;
    int32_t key_count;
} CompositorInputDevice;

// 触摸点信息
typedef struct {
    int32_t touch_id;
    float x, y;
    float pressure;
    float size;
    float tilt_x, tilt_y;
    uint64_t timestamp;
    int32_t state;
} TouchPoint;

// 手势事件数据
typedef struct {
    int32_t type;
    float x, y;
    float dx, dy;
    float scale;
    float rotation;
    int32_t touch_count;
} CompositorGestureEvent;

// 输入事件数据
typedef struct {
    int32_t type;
    int32_t device_id;
    int32_t device_type;
    int64_t timestamp;
    
    // 基本输入数据
    float x, y;
    float relative_x, relative_y;
    int32_t key;
    int32_t button;
    int32_t state;
    int32_t modifiers;
    uint32_t unicode;
    
    // 高级输入数据
    float pressure;
    float distance;
    float tilt_x, tilt_y;
    float rotation;
    
    // 触摸数据
    int32_t touch_count;
    TouchPoint* touches;
    
    // 手势数据
    int32_t gesture_type;
    float gesture_scale;
    float gesture_rotation;
    
    // 滚动数据
    float scroll_x, scroll_y;
    float scroll_distance_x, scroll_distance_y;
    float scroll_delta_z;
} CompositorInputEvent;

// 输入状态结构体
typedef struct {
    // 设备状态
    CompositorInputDevice* devices;
    int32_t device_count;
    
    // 按键状态
    bool* key_states;
    bool* button_states;
    int32_t modifiers;
    
    // 鼠标状态
    float mouse_x, mouse_y;
    float mouse_relative_x, mouse_relative_y;
    float mouse_wheel_x, mouse_wheel_y;
    float mouse_wheel_z;
    
    // 触摸状态
    TouchPoint* active_touches;
    int32_t max_touches;
    int32_t active_touch_count;
    
    // 手势状态
    bool gesture_active;
    int32_t current_gesture;
    float gesture_scale;
    float gesture_rotation;
    float gesture_threshold;
    bool gesture_enabled[COMPOSITOR_GESTURE_TWO_FINGER_TAP + 1];
    
    // 统计信息
    int64_t event_count;
    int64_t last_event_time;
} CompositorInputState;

// 注入输入事件（基础接口）
void compositor_handle_input(int type, int x, int y, int key, int state);

// 注入高级输入事件
void compositor_handle_input_event(const CompositorInputEvent* event);

// 注册输入设备
int compositor_register_input_device(const CompositorInputDevice* device);

// 注销输入设备
int compositor_unregister_input_device(int32_t device_id);

// 获取设备列表
int compositor_get_input_devices(CompositorInputDevice** devices, int32_t* count);

// 设置输入捕获模式
void compositor_set_input_capture_mode(bool capture_mouse, bool capture_keyboard);

// 获取当前鼠标位置
void compositor_get_mouse_position(float* x, float* y);

// 设置当前鼠标位置
void compositor_set_mouse_position(float x, float y);

// 显示/隐藏鼠标光标
void compositor_set_cursor_visibility(bool visible);

// 设置鼠标光标样式
void compositor_set_cursor_style(int style);

// 注入滚动事件
void compositor_handle_scroll(float dx, float dy, int device_id);

// 注入手势事件
void compositor_handle_gesture(int gesture_type, float scale, float rotation, float x, float y);

// 设置全局状态指针（内部使用）
void compositor_input_set_state(CompositorState* state);

// 清理输入相关资源
void compositor_input_cleanup(void);

// 手势相关设置函数
void compositor_input_set_gesture_enabled(int gesture_type, bool enabled);
bool compositor_input_is_gesture_enabled(int gesture_type);
void compositor_input_set_gesture_threshold(int gesture_type, float threshold);
float compositor_input_get_gesture_threshold(int gesture_type);

// 触摸相关查询函数
size_t compositor_input_get_active_touch_points(TouchPoint** points);

// 输入事件模拟函数
int compositor_input_simulate_keyboard(int key, int state);
int compositor_input_simulate_mouse_button(int button, int state);
int compositor_input_simulate_mouse_motion(float x, float y);
int compositor_input_simulate_touch(int touch_id, int state, float x, float y, float pressure);

// 设备查询函数
size_t compositor_input_get_device_count();
CompositorInputDevice** compositor_input_get_all_devices();
bool compositor_input_device_has_capability(int32_t device_id, int capability);

#endif // COMPOSITOR_INPUT_H