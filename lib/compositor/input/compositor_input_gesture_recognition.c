#include "compositor_input_gesture_recognition.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// 高级手势识别器配置
static CompositorGestureRecognizerConfig g_gesture_recognizer_config = {
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

// 获取当前时间（毫秒）
static int64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
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

// 手势识别初始化
int compositor_gesture_recognition_init(void) {
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    return COMPOSITOR_OK;
}

// 手势识别清理
void compositor_gesture_recognition_cleanup(void) {
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
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

// 处理手势开始
void handle_gesture_start(CompositorInputEvent* event) {
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

// 处理手势更新
void handle_gesture_update(CompositorInputEvent* event) {
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
void handle_gesture_end(void) {
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

// 获取活动触摸点数量
int compositor_input_get_active_touch_points(void) {
    return g_gesture_state.touch_count;
}