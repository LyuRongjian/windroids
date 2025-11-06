/*
 * WinDroids Compositor
 * Window Switching Module
 */

#ifndef COMPOSITOR_INPUT_WINDOW_SWITCH_H
#define COMPOSITOR_INPUT_WINDOW_SWITCH_H

#include "compositor.h"
#include <stdbool.h>

// 窗口切换功能函数声明

// 设置合成器状态引用（供内部使用）
void compositor_input_window_switch_set_state(CompositorState* state);

// 初始化窗口切换系统
int compositor_input_window_switch_init(void);

// 清理窗口切换系统
void compositor_input_window_switch_cleanup(void);

// 开始窗口切换（Alt+Tab）
int compositor_input_start_window_switch(void);

// 结束窗口切换
int compositor_input_end_window_switch(bool apply_selection);

// 选择下一个窗口
int compositor_input_select_next_window(void);

// 选择上一个窗口
int compositor_input_select_prev_window(void);

// 获取当前选中的窗口索引
int compositor_input_get_selected_window_index(void);

// 获取窗口列表
void** compositor_input_get_window_list(int* out_count, bool** out_is_wayland);

// 显示窗口预览
int compositor_input_show_window_previews(void);

// 隐藏窗口预览
void compositor_input_hide_window_previews(void);

// 收集所有可见窗口
void collect_visible_windows(void);

// 清理窗口列表
void cleanup_window_list(void);

#endif // COMPOSITOR_INPUT_WINDOW_SWITCH_H