#ifndef COMPOSITOR_VULKAN_ADAPT_H
#define COMPOSITOR_VULKAN_ADAPT_H

#include "compositor_vulkan_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 动态调整渲染质量
void adapt_rendering_quality(VulkanState* vulkan);

// 降低渲染质量以提高性能
void decrease_rendering_quality(VulkanState* vulkan);

// 提高渲染质量以改善视觉效果
void increase_rendering_quality(VulkanState* vulkan);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_VULKAN_ADAPT_H