#ifndef COMPOSITOR_VULKAN_INIT_H
#define COMPOSITOR_VULKAN_INIT_H

#include "compositor_vulkan_core.h"

// Vulkan初始化相关函数
int init_vulkan(CompositorState* state);
int load_vulkan_functions(VulkanState* vulkan);
int create_vulkan_instance(VulkanState* vulkan, bool enable_validation);
int select_physical_device(VulkanState* vulkan);
int create_logical_device(VulkanState* vulkan, bool enable_vsync);
int create_command_pool(VulkanState* vulkan);
int create_transfer_command_pool(VulkanState* vulkan);
int create_swapchain(VulkanState* vulkan, ANativeWindow* window, int width, int height);
int create_render_pass(VulkanState* vulkan);
int create_descriptor_set_layout(VulkanState* vulkan);
int create_pipeline_layout(VulkanState* vulkan);
int create_graphics_pipeline(VulkanState* vulkan);
int create_descriptor_pool(VulkanState* vulkan);
int create_descriptor_sets(VulkanState* vulkan);
int create_framebuffers(VulkanState* vulkan);
int create_command_buffers(VulkanState* vulkan);
int create_sync_objects(VulkanState* vulkan);
int create_vertex_buffer(VulkanState* vulkan);
int create_texture_cache(VulkanState* vulkan);

#endif // COMPOSITOR_VULKAN_INIT_H