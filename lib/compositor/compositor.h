#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <android/native_window.h>
#include <stdint.h>

// 包含拆分后的头文件
#include "compositor_config.h"
#include "compositor_window.h"
#include "compositor_input.h"
#include "compositor_utils.h"

#ifdef __cplusplus
extern "C"  {
#endif

// 初始化合成器
// - window: 来自 GameActivity.app->window
// - width/height: 建议从 ANativeWindow_getWidth/Height 获取
// - config: 配置参数，NULL 则使用默认配置
int compositor_init(ANativeWindow* window, int width, int height, CompositorConfig* config);

// 主循环单步（应在 GameActivity 的渲染线程中循环调用）
// 返回 0 表示正常，非 0 表示错误
int compositor_step(void);

// 销毁合成器
void compositor_destroy(void);

// 设置窗口大小
// - width/height: 新的窗口大小
// 返回: 0 成功，非 0 失败
int compositor_resize(int width, int height);

// 获取活动窗口标题
// 返回: 当前活动窗口的标题，如果没有活动窗口则返回NULL
const char* compositor_get_active_window_title();

// 触发重绘
void compositor_schedule_redraw(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_H