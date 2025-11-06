/*
 * WinDroids Compositor
 * Input Types and Definitions
 */

#ifndef COMPOSITOR_INPUT_TYPES_H
#define COMPOSITOR_INPUT_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 输入设备类型枚举
typedef enum {
    COMPOSITOR_INPUT_DEVICE_TYPE_KEYBOARD = 0,
    COMPOSITOR_INPUT_DEVICE_TYPE_MOUSE = 1,
    COMPOSITOR_INPUT_DEVICE_TYPE_TOUCHSCREEN = 2,
    COMPOSITOR_INPUT_DEVICE_TYPE_TOUCHPAD = 3,
    COMPOSITOR_INPUT_DEVICE_TYPE_PEN = 4,
    COMPOSITOR_INPUT_DEVICE_TYPE_GAMEPAD = 5,
    COMPOSITOR_INPUT_DEVICE_TYPE_JOYSTICK = 6,
    COMPOSITOR_INPUT_DEVICE_TYPE_TRACKBALL = 7,
    COMPOSITOR_INPUT_DEVICE_TYPE_UNKNOWN = 255
} CompositorInputDeviceType;

// 输入事件类型枚举
typedef enum {
    COMPOSITOR_INPUT_EVENT_NONE = 0,
    COMPOSITOR_INPUT_EVENT_KEY = 1,
    COMPOSITOR_INPUT_EVENT_BUTTON = 2,
    COMPOSITOR_INPUT_EVENT_MOTION = 3,
    COMPOSITOR_INPUT_EVENT_TOUCH = 4,
    COMPOSITOR_INPUT_EVENT_GESTURE = 5,
    COMPOSITOR_INPUT_EVENT_SCROLL = 6,
    COMPOSITOR_INPUT_EVENT_PROXIMITY = 7
} CompositorInputEventType;

// 输入状态枚举
typedef enum {
    COMPOSITOR_INPUT_STATE_RELEASED = 0,
    COMPOSITOR_INPUT_STATE_PRESSED = 1,
    COMPOSITOR_INPUT_STATE_TOUCH_BEGIN = 2,
    COMPOSITOR_INPUT_STATE_TOUCH_UPDATE = 3,
    COMPOSITOR_INPUT_STATE_TOUCH_END = 4
} CompositorInputState;

// 手势类型枚举
typedef enum {
    COMPOSITOR_GESTURE_NONE = 0,
    COMPOSITOR_GESTURE_TAP = 1,
    COMPOSITOR_GESTURE_DOUBLE_TAP = 2,
    COMPOSITOR_GESTURE_LONG_PRESS = 3,
    COMPOSITOR_GESTURE_DRAG = 4,
    COMPOSITOR_GESTURE_PINCH = 5,
    COMPOSITOR_GESTURE_ROTATE = 6,
    COMPOSITOR_GESTURE_SWIPE = 7
} CompositorGestureType;

// 输入捕获模式枚举
typedef enum {
    COMPOSITOR_INPUT_CAPTURE_NORMAL = 0,
    COMPOSITOR_INPUT_CAPTURE_FULLSCREEN = 1,
    COMPOSITOR_INPUT_CAPTURE_EXCLUSIVE = 2,
    COMPOSITOR_INPUT_CAPTURE_DISABLED = 3
} CompositorInputCaptureMode;

// 输入修饰符标志
typedef enum {
    COMPOSITOR_INPUT_MODIFIER_SHIFT = 1 << 0,
    COMPOSITOR_INPUT_MODIFIER_CTRL = 1 << 1,
    COMPOSITOR_INPUT_MODIFIER_ALT = 1 << 2,
    COMPOSITOR_INPUT_MODIFIER_SUPER = 1 << 3,
    COMPOSITOR_INPUT_MODIFIER_CAPS = 1 << 4,
    COMPOSITOR_INPUT_MODIFIER_NUM = 1 << 5
} CompositorInputModifier;

// 性能统计结构体
typedef struct {
    uint32_t total_events;
    uint32_t keyboard_events;
    uint32_t mouse_events;
    uint32_t touch_events;
    uint32_t gesture_events;
    uint32_t dropped_events;
    float average_processing_time;
    float max_processing_time;
    uint64_t start_time;
} CompositorInputPerformanceStats;

// 设备信息结构体
typedef struct {
    int32_t device_id;
    CompositorInputDeviceType type;
    char name[256];
    char vendor[128];
    char product[128];
    bool enabled;
    bool has_pressure;
    bool has_tilt;
    bool has_rotation;
    int32_t num_buttons;
    int32_t num_axes;
} CompositorInputDeviceInfo;

// 触摸点信息结构体
typedef struct {
    int32_t id;
    float x;
    float y;
    float pressure;
    float major;
    float minor;
    float orientation;
    CompositorInputState state;
    uint64_t timestamp;
} CompositorTouchPoint;

// 手势信息结构体
typedef struct {
    CompositorGestureType type;
    float x;
    float y;
    float dx;
    float dy;
    float scale;
    float rotation;
    int32_t num_fingers;
    uint64_t timestamp;
} CompositorGestureInfo;

// 输入配置结构体
typedef struct {
    bool enable_gestures;
    bool enable_shortcuts;
    bool enable_window_dragging;
    bool enable_touch_feedback;
    uint32_t double_click_timeout_ms;
    uint32_t long_press_timeout_ms;
    float drag_threshold;
    float scroll_threshold;
} CompositorInputConfig;

// 输入事件结构体
typedef struct {
    CompositorInputEventType type;
    uint64_t timestamp;
    int32_t device_id;
    
    union {
        struct {
            uint32_t keycode;
            CompositorInputState state;
            uint32_t modifiers;
        } keyboard;
        
        struct {
            int32_t button;
            CompositorInputState state;
            float x;
            float y;
        } mouse;
        
        struct {
            float x;
            float y;
            float dx;
            float dy;
        } motion;
        
        struct {
            int32_t id;
            float x;
            float y;
            float pressure;
            CompositorInputState state;
        } touch;
        
        struct {
            CompositorGestureType type;
            float x;
            float y;
            float dx;
            float dy;
            float scale;
            float rotation;
            int32_t num_fingers;
        } gesture;
        
        struct {
            float x;
            float y;
            float dx;
            float dy;
            int32_t axis;
        } scroll;
    };
} CompositorInputEvent;

// 输入事件处理回调函数类型
typedef void (*CompositorInputEventCallback)(const CompositorInputEvent* event, void* user_data);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_INPUT_TYPES_H