#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <android/native_window.h>
#include <stdbool.h>
#include "compositor_perf_opt.h"
#include "compositor_game.h"
#include "compositor_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化合成器
// - window: 来自 GameActivity.app->window
// - width/height: 建议从 ANativeWindow_getWidth/Height 获取
int compositor_init(ANativeWindow* window, int width, int height);

// 主循环单步（应在 GameActivity 的渲染线程中循环调用）
// 返回 0 表示正常，非 0 表示错误
int compositor_step(void);

// 销毁合成器
void compositor_destroy(void);

// 注入输入事件（触摸、键盘等）
void compositor_handle_input(int type, int x, int y, int key, int state);

// 处理Android输入事件
void compositor_handle_android_input_event(int type, int x, int y, int state);

// 处理Android鼠标事件
void compositor_handle_android_mouse_event(float x, float y, int button, bool down);

// 处理Android鼠标滚轮事件
void compositor_handle_android_mouse_scroll(float delta_x, float delta_y);

// 处理Android键盘事件
void compositor_handle_android_keyboard_event(uint32_t keycode, uint32_t modifiers, bool down);

// 处理Android游戏手柄按钮事件
void compositor_handle_android_gamepad_button_event(uint32_t device_id, int button, bool down);

// 处理Android游戏手柄轴事件
void compositor_handle_android_gamepad_axis_event(uint32_t device_id, int axis, float value);

// 设置窗口大小
void compositor_set_size(int width, int height);

// 获取当前帧率
float compositor_get_fps(void);

// 性能优化相关API
int compositor_set_perf_opt_enabled(bool enabled);
bool compositor_is_perf_opt_enabled(void);
int compositor_set_perf_profile(enum perf_profile profile);
enum perf_profile compositor_get_perf_profile(void);
int compositor_set_adaptive_fps_enabled(bool enabled);
bool compositor_is_adaptive_fps_enabled(void);
int compositor_set_adaptive_quality_enabled(bool enabled);
bool compositor_is_adaptive_quality_enabled(void);
int compositor_set_target_fps(int fps);
int compositor_get_target_fps(void);
int compositor_set_quality_level(int level);
int compositor_get_quality_level(void);
int compositor_get_perf_opt_stats(struct perf_opt_stats *stats);

// 游戏模式相关API
int compositor_set_game_mode_enabled(bool enabled);
bool compositor_is_game_mode_enabled(void);
int compositor_set_game_type(enum game_type type);
enum game_type compositor_get_game_type(void);
int compositor_set_input_boost_enabled(bool enabled);
bool compositor_is_input_boost_enabled(void);
int compositor_set_priority_boost_enabled(bool enabled);
bool compositor_is_priority_boost_enabled(void);
int compositor_get_game_mode_stats(struct game_mode_stats *stats);

// 监控相关API
int compositor_set_monitoring_enabled(bool enabled);
bool compositor_is_monitoring_enabled(void);
int compositor_set_auto_save_enabled(bool enabled);
bool compositor_is_auto_save_enabled(void);
int compositor_set_sample_interval(int interval_ms);
int compositor_get_sample_interval(void);
int compositor_set_report_interval(int interval_ms);
int compositor_get_report_interval(void);
int compositor_save_report(const char *path);
int compositor_get_monitor_stats(struct monitor_stats *stats);
int compositor_get_monitor_report(struct monitor_report *report);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_H