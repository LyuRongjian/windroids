#ifndef COMPOSITOR_VULKAN_RENDER_H
#define COMPOSITOR_VULKAN_RENDER_H

#include "compositor_vulkan_core.h"

// 渲染相关函数
int render_frame(void);
int recreate_swapchain(int width, int height);
int render_dirty_rects(CompositorState* state, VkCommandBuffer command_buffer);
int render_with_scissors(CompositorState* state, VkCommandBuffer command_buffer, const DirtyRect* rects, int count);

// 内部渲染函数
void render_background(CompositorState* state, VkCommandBuffer command_buffer);
void render_windows(CompositorState* state, VkCommandBuffer command_buffer);
void render_window(CompositorState* state, WindowInfo* window, bool is_wayland, VkCommandBuffer command_buffer);
void render_window_with_dirty_rects(CompositorState* state, WindowInfo* window, bool is_wayland, VkCommandBuffer command_buffer);

// 多窗口渲染函数
int prepare_window_rendering(CompositorState* state, void* window_ptr, bool is_wayland);
int finish_window_rendering(CompositorState* state, void* window_ptr, bool is_wayland);

// 使用硬件加速渲染窗口
int render_windows_with_hardware_acceleration(CompositorState* state);

// 内部函数
void wait_idle(void);
int acquire_next_image(VulkanState* vulkan, uint32_t* image_index);
int begin_rendering(VulkanState* vulkan, uint32_t image_index);
int end_rendering(VulkanState* vulkan);
int submit_rendering(VulkanState* vulkan, uint32_t image_index);

#endif // COMPOSITOR_VULKAN_RENDER_H