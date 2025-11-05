/*
 * WinDroids Compositor
 * Gesture Recognition Implementation
 */

#include "compositor_input_gesture.h"
#include "compositor_input.h"
#include "compositor_utils.h"
#include <string.h>
#include <math.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

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

// 手势识别器配置
static CompositorGestureConfig g_gesture_config = {
    .double_tap_timeout = 300,
    .long_press_timeout = 500,
    .tap_threshold = 10.0f,
    .swipe_threshold = 50.0f,
    .pinch_threshold = 0.1f,
    .rotation_threshold = 5.0f,
    .velocity_threshold = 100.0f
};

// 手势回调相关
#define MAX_GESTURE_CALLBACKS 8
static struct {
    GestureCallback callback;
    void* user_data;
} g_gesture_callbacks[MAX_GESTURE_CALLBACKS];
static int g_callback_count = 0;

// 设置合成器状态引用
void compositor_input_gesture_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 初始化手势识别系统
int compositor_input_gesture_init(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 初始化手势状态
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    
    // 初始化回调
    memset(g_gesture_callbacks, 0, sizeof(g_gesture_callbacks));
    g_callback_count = 0;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Gesture recognition system initialized");
    return COMPOSITOR_OK;
}

// 清理手势识别系统
void compositor_input_gesture_cleanup(void) {
    // 清理资源
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    memset(g_gesture_callbacks, 0, sizeof(g_gesture_callbacks));
    g_callback_count = 0;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Gesture recognition system cleaned up");
}

// 设置手势配置
void compositor_input_gesture_set_config(const CompositorGestureConfig* config) {
    if (config) {
        g_gesture_config = *config;
        log_message(COMPOSITOR_LOG_DEBUG, "Gesture config updated");
    }
}

// 获取当前手势配置
void compositor_input_gesture_get_config(CompositorGestureConfig* config) {
    if (config) {
        *config = g_gesture_config;
    }
}

// 触发手势回调
static void trigger_gesture_callback(CompositorGestureType type) {
    for (int i = 0; i < g_callback_count; i++) {
        if (g_gesture_callbacks[i].callback) {
            g_gesture_callbacks[i].callback(type, g_gesture_callbacks[i].user_data);
        }
    }
}

// 计算两点之间的距离
static float calculate_distance(int x1, int y1, int x2, int y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

// 计算两点之间的角度
static float calculate_angle(int x1, int y1, int x2, int y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return atan2(dy, dx) * 180.0f / M_PI;
}

// 处理触摸点事件（用于手势识别）
int compositor_input_gesture_process_touch(int touch_id, int x, int y, bool pressed, int64_t timestamp) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 限制触摸点数量
    if (touch_id >= 10) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 更新当前触摸点位置
    g_gesture_state.current_x[touch_id] = x;
    g_gesture_state.current_y[touch_id] = y;
    g_gesture_state.last_update_time = timestamp;
    
    if (pressed) {
        // 新的触摸点开始
        g_gesture_state.start_x[touch_id] = x;
        g_gesture_state.start_y[touch_id] = y;
        g_gesture_state.touch_count++;
        
        if (!g_gesture_state.is_active) {
            g_gesture_state.is_active = true;
            g_gesture_state.start_time = timestamp;
            g_gesture_state.scale = 1.0f;
            g_gesture_state.rotation = 0.0f;
        }
        
        // 检测点击和双击
        int64_t time_since_last_click = timestamp - g_gesture_state.last_click_time;
        float distance_from_last_click = calculate_distance(
            x, y, g_gesture_state.last_click_x, g_gesture_state.last_click_y);
        
        if (time_since_last_click < g_gesture_config.double_tap_timeout && 
            distance_from_last_click < g_gesture_config.tap_threshold) {
            g_gesture_state.click_count++;
            if (g_gesture_state.click_count == 2) {
                g_gesture_state.type = GESTURE_TYPE_DOUBLE_TAP;
                trigger_gesture_callback(GESTURE_TYPE_DOUBLE_TAP);
                g_gesture_state.click_count = 0; // 重置点击计数
            }
        } else {
            g_gesture_state.click_count = 1;
        }
        
        g_gesture_state.last_click_time = timestamp;
        g_gesture_state.last_click_x = x;
        g_gesture_state.last_click_y = y;
        
    } else {
        // 触摸点结束
        g_gesture_state.touch_count--;
        
        // 检查是否为简单点击
        float distance = calculate_distance(
            g_gesture_state.start_x[touch_id], 
            g_gesture_state.start_y[touch_id],
            x, y);
        
        int64_t press_duration = timestamp - g_gesture_state.start_time;
        
        if (distance < g_gesture_config.tap_threshold) {
            if (press_duration >= g_gesture_config.long_press_timeout) {
                g_gesture_state.type = GESTURE_TYPE_LONG_PRESS;
                trigger_gesture_callback(GESTURE_TYPE_LONG_PRESS);
            } else if (g_gesture_state.click_count == 1) {
                // 确保不是双击的一部分
                g_gesture_state.type = GESTURE_TYPE_TAP;
                trigger_gesture_callback(GESTURE_TYPE_TAP);
            }
        } else if (distance >= g_gesture_config.swipe_threshold) {
            // 检测滑动方向
            int dx = x - g_gesture_state.start_x[touch_id];
            int dy = y - g_gesture_state.start_y[touch_id];
            
            if (abs(dx) > abs(dy)) {
                // 水平滑动
                if (dx > 0) {
                    g_gesture_state.type = GESTURE_TYPE_SWIPE_RIGHT;
                    trigger_gesture_callback(GESTURE_TYPE_SWIPE_RIGHT);
                } else {
                    g_gesture_state.type = GESTURE_TYPE_SWIPE_LEFT;
                    trigger_gesture_callback(GESTURE_TYPE_SWIPE_LEFT);
                }
            } else {
                // 垂直滑动
                if (dy > 0) {
                    g_gesture_state.type = GESTURE_TYPE_SWIPE_DOWN;
                    trigger_gesture_callback(GESTURE_TYPE_SWIPE_DOWN);
                } else {
                    g_gesture_state.type = GESTURE_TYPE_SWIPE_UP;
                    trigger_gesture_callback(GESTURE_TYPE_SWIPE_UP);
                }
            }
        }
        
        if (g_gesture_state.touch_count == 0) {
            // 所有触摸点都结束了，重置手势状态
            compositor_input_gesture_reset();
        }
    }
    
    // 处理多点触控手势（缩放、旋转）
    if (g_gesture_state.touch_count >= 2) {
        // 简化实现，只使用前两个触摸点
        if (touch_id < 2) {
            int t0 = 0, t1 = 1;
            
            // 计算当前两点之间的距离和角度
            float current_dist = calculate_distance(
                g_gesture_state.current_x[t0], g_gesture_state.current_y[t0],
                g_gesture_state.current_x[t1], g_gesture_state.current_y[t1]);
            
            float current_angle = calculate_angle(
                g_gesture_state.current_x[t0], g_gesture_state.current_y[t0],
                g_gesture_state.current_x[t1], g_gesture_state.current_y[t1]);
            
            // 计算起始两点之间的距离和角度
            float start_dist = calculate_distance(
                g_gesture_state.start_x[t0], g_gesture_state.start_y[t0],
                g_gesture_state.start_x[t1], g_gesture_state.start_y[t1]);
            
            float start_angle = calculate_angle(
                g_gesture_state.start_x[t0], g_gesture_state.start_y[t0],
                g_gesture_state.start_x[t1], g_gesture_state.start_y[t1]);
            
            // 计算缩放因子
            g_gesture_state.scale = current_dist / start_dist;
            
            // 计算旋转角度
            g_gesture_state.rotation = current_angle - start_angle;
            
            // 检测缩放手势
            if (fabs(g_gesture_state.scale - 1.0f) > g_gesture_config.pinch_threshold) {
                if (g_gesture_state.scale > 1.0f) {
                    g_gesture_state.type = GESTURE_TYPE_PINCH_OUT;
                    trigger_gesture_callback(GESTURE_TYPE_PINCH_OUT);
                } else {
                    g_gesture_state.type = GESTURE_TYPE_PINCH_IN;
                    trigger_gesture_callback(GESTURE_TYPE_PINCH_IN);
                }
            }
            
            // 检测旋转手势
            if (fabs(g_gesture_state.rotation) > g_gesture_config.rotation_threshold) {
                g_gesture_state.type = GESTURE_TYPE_ROTATE;
                trigger_gesture_callback(GESTURE_TYPE_ROTATE);
            }
        }
    }
    
    // 计算速度
    if (g_gesture_state.is_active && g_gesture_state.last_update_time > g_gesture_state.start_time) {
        int64_t delta_time = g_gesture_state.last_update_time - g_gesture_state.start_time;
        if (delta_time > 0) {
            float time_seconds = delta_time / 1000.0f;
            int dx = g_gesture_state.current_x[0] - g_gesture_state.start_x[0];
            int dy = g_gesture_state.current_y[0] - g_gesture_state.start_y[0];
            
            g_gesture_state.velocity_x = dx / time_seconds;
            g_gesture_state.velocity_y = dy / time_seconds;
        }
    }
    
    return COMPOSITOR_OK;
}

// 获取当前活动手势
CompositorGestureType compositor_input_gesture_get_active(void) {
    return g_gesture_state.type;
}

// 获取手势参数（如缩放、旋转等）
float compositor_input_gesture_get_scale(void) {
    return g_gesture_state.scale;
}

float compositor_input_gesture_get_rotation(void) {
    return g_gesture_state.rotation;
}

float compositor_input_gesture_get_velocity_x(void) {
    return g_gesture_state.velocity_x;
}

float compositor_input_gesture_get_velocity_y(void) {
    return g_gesture_state.velocity_y;
}

// 重置手势状态
void compositor_input_gesture_reset(void) {
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    g_gesture_state.is_active = false;
    g_gesture_state.type = GESTURE_TYPE_NONE;
    g_gesture_state.scale = 1.0f;
}

// 注册手势回调函数
void compositor_input_gesture_register_callback(GestureCallback callback, void* user_data) {
    if (callback && g_callback_count < MAX_GESTURE_CALLBACKS) {
        g_gesture_callbacks[g_callback_count].callback = callback;
        g_gesture_callbacks[g_callback_count].user_data = user_data;
        g_callback_count++;
    }
}

void compositor_input_gesture_unregister_callback(GestureCallback callback) {
    for (int i = 0; i < g_callback_count; i++) {
        if (g_gesture_callbacks[i].callback == callback) {
            // 将最后一个回调移到当前位置，然后减少计数
            if (i < g_callback_count - 1) {
                g_gesture_callbacks[i] = g_gesture_callbacks[g_callback_count - 1];
            }
            g_callback_count--;
            break;
        }
    }
}