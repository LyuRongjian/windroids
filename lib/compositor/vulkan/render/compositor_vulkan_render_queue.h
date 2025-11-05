#ifndef COMPOSITOR_VULKAN_RENDER_QUEUE_H
#define COMPOSITOR_VULKAN_RENDER_QUEUE_H

#include "compositor_vulkan_core.h"

// 渲染队列管理函数
int init_render_queue(VulkanState* vulkan);
int add_render_command(VulkanState* vulkan, RenderCommandType type, void* data);
int execute_render_queue(VulkanState* vulkan, VkCommandBuffer command_buffer);

// 初始化渲染批次管理
int init_render_batches(VulkanState* vulkan);

// 优化渲染批次
int optimize_render_batches(VulkanState* vulkan);

#endif // COMPOSITOR_VULKAN_RENDER_QUEUE_H