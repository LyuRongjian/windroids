/*
 * WinDroids Compositor
 * Window Preview Rendering Interface
 */

#ifndef COMPOSITOR_WINDOW_PREVIEW_H
#define COMPOSITOR_WINDOW_PREVIEW_H

#include <stdint.h>
#include <stdbool.h>
#include "../compositor.h"
#include "../compositor_window.h"
#include "../vulkan/compositor_vulkan.h"

// 窗口预览初始化和清理
int compositor_window_preview_init(void);
void compositor_window_preview_cleanup(void);

// 设置合成器状态引用
void compositor_window_preview_set_state(CompositorState* state);

// 窗口预览管理
int compositor_window_preview_set_windows(void** windows, bool* is_wayland, int count);
int compositor_window_preview_set_selected(int index);
int compositor_window_preview_show(void);
void compositor_window_preview_hide(void);

// 渲染
void compositor_window_preview_render(void);

// 查询
void* compositor_window_preview_get_selected_window(bool* out_is_wayland);
bool compositor_window_preview_is_visible(void);
int compositor_window_preview_get_count(void);

#endif // COMPOSITOR_WINDOW_PREVIEW_H