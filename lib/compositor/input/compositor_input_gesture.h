/*
 * WinDroids Compositor
 * Gesture Recognition Module
 */

#ifndef COMPOSITOR_INPUT_GESTURE_H
#define COMPOSITOR_INPUT_GESTURE_H

#include "compositor.h"
#include <stdbool.h>

// 手势类型枚举
typedef enum {
    GESTURE_TYPE_NONE,
    GESTURE_TYPE_TAP,
    GESTURE_TYPE_DOUBLE_TAP,
    GESTURE_TYPE_LONG_PRESS,
    GESTURE_TYPE_SWIPE_LEFT,
    GESTURE_TYPE_SWIPE_RIGHT,
    GESTURE_TYPE_SWIPE_UP,
    GESTURE_TYPE_SWIPE_DOWN,
    GESTURE_TYPE_PINCH_IN,
    GESTURE_TYPE_PINCH_OUT,
    GESTURE_TYPE_ROTATE,
    GESTURE_TYPE_DRAG
} CompositorGestureType;

// 手势配置结构体
typedef struct {
    int32_t double_tap_timeout;      // 双击超时时间（毫秒）
    int32_t long_press_timeout;      // 长按超时时间（毫秒）
    float tap_threshold;             // 点击阈值（像素）
    float swipe_threshold;           // 滑动阈值（像素）
    float pinch_threshold;           // 捏合阈值（缩放比例）
    float rotation_threshold;        // 旋转阈值（度）
    float velocity_threshold;        // 速度阈值（像素/秒）
} CompositorGestureConfig;

// 手势识别函数声明

// 设置合成器状态引用（供内部使用）
void compositor_input_gesture_set_state(CompositorState* state);

// 初始化手势识别系统
int compositor_input_gesture_init(void);

// 清理手势识别系统
void compositor_input_gesture_cleanup(void);

// 设置手势配置
void compositor_input_gesture_set_config(const CompositorGestureConfig* config);

// 获取当前手势配置
void compositor_input_gesture_get_config(CompositorGestureConfig* config);

// 处理触摸点事件（用于手势识别）
int compositor_input_gesture_process_touch(int touch_id, int x, int y, bool pressed, int64_t timestamp);

// 获取当前活动手势
CompositorGestureType compositor_input_gesture_get_active(void);

// 获取手势参数（如缩放、旋转等）
float compositor_input_gesture_get_scale(void);
float compositor_input_gesture_get_rotation(void);
float compositor_input_gesture_get_velocity_x(void);
float compositor_input_gesture_get_velocity_y(void);

// 重置手势状态
void compositor_input_gesture_reset(void);

// 注册手势回调函数
typedef void (*GestureCallback)(CompositorGestureType type, void* user_data);
void compositor_input_gesture_register_callback(GestureCallback callback, void* user_data);
void compositor_input_gesture_unregister_callback(GestureCallback callback);

#endif // COMPOSITOR_INPUT_GESTURE_H