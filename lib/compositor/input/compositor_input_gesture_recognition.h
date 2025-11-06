#ifndef COMPOSITOR_INPUT_GESTURE_RECOGNITION_H
#define COMPOSITOR_INPUT_GESTURE_RECOGNITION_H

#include "compositor_input_types.h"
#include "compositor_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

// 手势识别器配置
typedef struct {
    int32_t double_tap_timeout;      // 双击超时时间（毫秒）
    int32_t long_press_timeout;      // 长按超时时间（毫秒）
    float tap_threshold;             // 点击阈值（像素）
    float swipe_threshold;           // 滑动阈值（像素）
    float pinch_threshold;           // 捏合阈值（缩放比例）
    float rotation_threshold;        // 旋转阈值（度）
    float velocity_threshold;        // 速度阈值（像素/秒）
} CompositorGestureRecognizerConfig;

// 手势识别初始化和清理
int compositor_gesture_recognition_init(void);
void compositor_gesture_recognition_cleanup(void);

// 手势识别配置
void compositor_input_set_gesture_config(int32_t double_tap_timeout, int32_t long_press_timeout, 
                                        float tap_threshold, float swipe_threshold);

// 手势处理函数
void handle_gesture_start(CompositorInputEvent* event);
void handle_gesture_update(CompositorInputEvent* event);
void handle_gesture_end(void);

// 获取活动触摸点数量
int compositor_input_get_active_touch_points(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_INPUT_GESTURE_RECOGNITION_H