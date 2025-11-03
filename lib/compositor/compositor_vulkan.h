#ifndef COMPOSITOR_VULKAN_H
#define COMPOSITOR_VULKAN_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

// Vulkan初始化相关函数
bool init_vulkan(void);
bool load_vulkan_functions(void);
void cleanup_vulkan(void);
bool recreate_swapchain(void);

#endif // COMPOSITOR_VULKAN_H