/*
 * WinDroids Compositor
 * Vulkan Rendering Module
 */

#ifndef COMPOSITOR_VULKAN_RENDER_H
#define COMPOSITOR_VULKAN_RENDER_H

#include "compositor.h"
#include <vulkan/vulkan.h>

// Vulkan渲染函数声明

// 设置合成器状态引用（供内部使用）
void compositor_vulkan_render_set_state(CompositorState* state);

// 重建交换链
int rebuild_swapchain(void);

// 渲染一帧
int render_frame(void);

// 准备渲染命令
int prepare_render_commands(VkCommandBuffer command_buffer, uint32_t image_index);

// 绘制背景
void draw_background(VkCommandBuffer command_buffer);

// 绘制窗口
void draw_window(VkCommandBuffer command_buffer, void* window, bool is_wayland);

// 应用脏区域优化
void apply_dirty_region_optimization(VkCommandBuffer command_buffer, void* window);

// 更新窗口纹理
int update_window_texture(void* window, bool is_wayland);

// 重新排序窗口（按Z轴）
void sort_windows_by_z_order(void);

// 调度重绘
void schedule_redraw(void);

// 获取窗口纹理
VkImageView get_window_texture(void* window, bool is_wayland);

// 应用视口和裁剪矩形
void apply_viewport_and_scissor(VkCommandBuffer command_buffer, int x, int y, int width, int height);

#endif // COMPOSITOR_VULKAN_RENDER_H