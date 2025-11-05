/*
 * WinDroids Compositor - Render Module
 * Handles frame rendering, Vulkan integration, and rendering optimization
 */

#ifndef COMPOSITOR_RENDER_H
#define COMPOSITOR_RENDER_H

#include "compositor.h"

// 设置合成器状态引用（内部使用）
void compositor_render_set_state(CompositorState* state);

// 渲染单帧
int render_frame(void);

// 初始化Vulkan
int init_vulkan(CompositorState* state);

// 清理Vulkan资源
void cleanup_vulkan(void);

// 重建交换链
int recreate_swapchain(int width, int height);

// 触发重绘
void compositor_schedule_redraw(void);

// 渲染性能统计相关
void update_performance_stats(void);

#endif // COMPOSITOR_RENDER_H