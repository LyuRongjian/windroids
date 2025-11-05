/*
 * WinDroids Compositor - Dirty Region Management
 * Handles dirty rectangle tracking and optimization for rendering
 */

#ifndef COMPOSITOR_DIRTY_H
#define COMPOSITOR_DIRTY_H

#include "compositor.h"

// 设置合成器状态引用（内部使用）
void compositor_dirty_set_state(CompositorState* state);

// 标记脏区域
void compositor_mark_dirty_rect(int x, int y, int width, int height);

// 清除脏区域
void compositor_clear_dirty_rects(void);

// 合并脏区域
void merge_dirty_rects(CompositorState* state);

#endif // COMPOSITOR_DIRTY_H