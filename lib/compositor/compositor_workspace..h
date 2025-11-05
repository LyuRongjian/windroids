/*
 * WinDroids Compositor
 * Workspace management module
 */

#ifndef COMPOSITOR_WORKSPACE_H
#define COMPOSITOR_WORKSPACE_H

#include "compositor.h"

// 工作区管理相关函数声明
int compositor_create_workspace(const char* name);
int compositor_switch_workspace(int workspace_index);
int compositor_move_window_to_workspace(const char* window_title, int workspace_index);
int compositor_tile_windows(int tile_mode);
int compositor_cascade_windows();
int compositor_group_windows(const char** window_titles, int count, const char* group_name);
int compositor_ungroup_windows(const char* group_name);

// 内部辅助函数
void* find_window_by_title(const char* title, bool* out_is_wayland);
int collect_visible_windows(void*** windows, bool** is_wayland, int max_count);

// 设置合成器状态引用（供内部使用）
void compositor_workspace_set_state(CompositorState* state);

#endif // COMPOSITOR_WORKSPACE_H