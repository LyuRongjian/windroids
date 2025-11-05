/*
 * WinDroids Compositor
 * Vulkan Texture Cache Management Module
 */

#ifndef COMPOSITOR_VULKAN_TEXTURE_H
#define COMPOSITOR_VULKAN_TEXTURE_H

#include "compositor.h"
#include <vulkan/vulkan.h>

// 纹理缓存管理函数声明

// 设置合成器状态引用（供内部使用）
void compositor_vulkan_texture_set_state(CompositorState* state);

// 初始化纹理缓存
int init_texture_cache(void);

// 清理纹理缓存
void cleanup_texture_cache(void);

// 创建新纹理
VulkanTexture* create_texture(int width, int height, VkFormat format);

// 销毁纹理
void destroy_texture(VulkanTexture* texture);

// 更新纹理数据
int update_texture_data(VulkanTexture* texture, const void* data, size_t data_size);

// 从缓存中获取纹理
VulkanTexture* get_texture_from_cache(int width, int height, VkFormat format);

// 将纹理添加到缓存
void add_texture_to_cache(VulkanTexture* texture);

// 从缓存中移除纹理
void remove_texture_from_cache(VulkanTexture* texture);

// 清理未使用的纹理
void cleanup_unused_textures(void);

// 获取纹理缓存统计信息
void get_texture_cache_stats(TextureCacheStats* stats);

// 创建采样器
VkSampler create_sampler(void);

// 销毁采样器
void destroy_sampler(VkSampler sampler);

// 创建图像视图
VkImageView create_image_view(VkImage image, VkFormat format);

// 销毁图像视图
void destroy_image_view(VkImageView image_view);

#endif // COMPOSITOR_VULKAN_TEXTURE_H