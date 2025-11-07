#ifndef COMPOSITOR_VULKAN_H
#define COMPOSITOR_VULKAN_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/native_window.h>

#ifdef __cplusplus
extern "C" {
#endif

// Vulkan状态结构
struct vulkan_state {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkDevice device;
    VkQueue queue;
    VkSwapchainKHR swapchain;
    VkImage* images;
    VkImageView* image_views;
    VkFramebuffer* framebuffers;
    VkRenderPass render_pass;
    VkCommandPool command_pool;
    VkCommandBuffer* command_buffers;
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkFence* in_flight_fences;
    uint32_t image_count;
    uint32_t current_image;
    uint32_t queue_family_index;
    bool initialized;
    int width, height;
};

// 初始化Vulkan
int vulkan_init(struct vulkan_state* vk, ANativeWindow* window, int width, int height);

// 销毁Vulkan
void vulkan_destroy(struct vulkan_state* vk);

// 重建交换链
int vulkan_recreate_swapchain(struct vulkan_state* vk, int width, int height);

// 开始渲染帧
int vulkan_begin_frame(struct vulkan_state* vk, uint32_t* image_index);

// 结束渲染帧
int vulkan_end_frame(struct vulkan_state* vk, uint32_t image_index);

// 获取当前帧缓冲区
VkFramebuffer vulkan_get_current_framebuffer(struct vulkan_state* vk, uint32_t image_index);

// 获取命令缓冲区
VkCommandBuffer vulkan_get_command_buffer(struct vulkan_state* vk, uint32_t image_index);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_VULKAN_H