#include "compositor_vulkan.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "Vulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 验证层
static const char* validation_layers[] = {
    "VK_LAYER_KHRONOS_validation"
};

// 设备扩展
static const char* device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// 启用验证层
static bool enable_validation_layers = false;

// 检查验证层支持
static bool check_validation_layer_support(void) {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    
    VkLayerProperties* available_layers = (VkLayerProperties*)malloc(sizeof(VkLayerProperties) * layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers);
    
    for (size_t i = 0; i < sizeof(validation_layers) / sizeof(validation_layers[0]); i++) {
        bool layer_found = false;
        
        for (uint32_t j = 0; j < layer_count; j++) {
            if (strcmp(validation_layers[i], available_layers[j].layerName) == 0) {
                layer_found = true;
                break;
            }
        }
        
        if (!layer_found) {
            free(available_layers);
            return false;
        }
    }
    
    free(available_layers);
    return true;
}

// 创建Vulkan实例
static int create_vulkan_instance(struct vulkan_state* vk) {
    if (enable_validation_layers && !check_validation_layer_support()) {
        LOGE("Validation layers requested, but not available!");
        return -1;
    }
    
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "Windroids Compositor",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0
    };
    
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = NULL
    };
    
    // 获取Android所需的扩展
    const char* extensions[] = {
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
    
    create_info.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);
    create_info.ppEnabledExtensionNames = extensions;
    
    if (enable_validation_layers) {
        create_info.enabledLayerCount = sizeof(validation_layers) / sizeof(validation_layers[0]);
        create_info.ppEnabledLayerNames = validation_layers;
    }
    
    VkResult result = vkCreateInstance(&create_info, NULL, &vk->instance);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create Vulkan instance: %d", result);
        return -1;
    }
    
    return 0;
}

// 创建Android表面
static int create_android_surface(struct vulkan_state* vk, ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .window = window
    };
    
    VkResult result = vkCreateAndroidSurfaceKHR(vk->instance, &create_info, NULL, &vk->surface);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create Android surface: %d", result);
        return -1;
    }
    
    return 0;
}

// 选择物理设备
static int pick_physical_device(struct vulkan_state* vk) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(vk->instance, &device_count, NULL);
    
    if (device_count == 0) {
        LOGE("Failed to find GPUs with Vulkan support!");
        return -1;
    }
    
    VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(vk->instance, &device_count, devices);
    
    // 简化实现：选择第一个设备
    VkPhysicalDevice physical_device = devices[0];
    free(devices);
    
    // 查找队列族
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);
    
    VkQueueFamilyProperties* queue_families = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);
    
    // 查找支持图形和呈现的队列族
    for (uint32_t i = 0; i < queue_family_count; i++) {
        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, vk->surface, &present_support);
        
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && present_support) {
            vk->queue_family_index = i;
            break;
        }
    }
    
    free(queue_families);
    
    if (vk->queue_family_index == UINT32_MAX) {
        LOGE("Failed to find a suitable queue family!");
        return -1;
    }
    
    // 创建逻辑设备
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = vk->queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &(float){1.0f}
    };
    
    VkPhysicalDeviceFeatures device_features = {
        .fillModeNonSolid = VK_TRUE,
    };
    
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = sizeof(device_extensions) / sizeof(device_extensions[0]),
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = &device_features
    };
    
    if (enable_validation_layers) {
        device_create_info.enabledLayerCount = sizeof(validation_layers) / sizeof(validation_layers[0]);
        device_create_info.ppEnabledLayerNames = validation_layers;
    }
    
    VkResult result = vkCreateDevice(physical_device, NULL, &device_create_info, &vk->device);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create logical device: %d", result);
        return -1;
    }
    
    // 获取队列
    vkGetDeviceQueue(vk->device, vk->queue_family_index, 0, &vk->queue);
    
    return 0;
}

// 创建交换链
static int create_swapchain(struct vulkan_state* vk) {
    // 查询交换链支持
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->physical_device, vk->surface, &capabilities);
    
    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk->physical_device, vk->surface, &format_count, NULL);
    
    VkSurfaceFormatKHR* formats = (VkSurfaceFormatKHR*)malloc(sizeof(VkSurfaceFormatKHR) * format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk->physical_device, vk->surface, &format_count, formats);
    
    // 选择表面格式
    VkSurfaceFormatKHR surface_format = formats[0];
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = formats[i];
            break;
        }
    }
    
    free(formats);
    
    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk->physical_device, vk->surface, &present_mode_count, NULL);
    
    VkPresentModeKHR* present_modes = (VkPresentModeKHR*)malloc(sizeof(VkPresentModeKHR) * present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk->physical_device, vk->surface, &present_mode_count, present_modes);
    
    // 选择呈现模式
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < present_mode_count; i++) {
        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = present_modes[i];
            break;
        }
    }
    
    free(present_modes);
    
    // 设置范围
    VkExtent2D extent;
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent = capabilities.currentExtent;
    } else {
        extent.width = vk->width;
        extent.height = vk->height;
        
        if (extent.width < capabilities.minImageExtent.width) {
            extent.width = capabilities.minImageExtent.width;
        } else if (extent.width > capabilities.maxImageExtent.width) {
            extent.width = capabilities.maxImageExtent.width;
        }
        
        if (extent.height < capabilities.minImageExtent.height) {
            extent.height = capabilities.minImageExtent.height;
        } else if (extent.height > capabilities.maxImageExtent.height) {
            extent.height = capabilities.maxImageExtent.height;
        }
    }
    
    // 设置图像数量
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }
    
    // 创建交换链
    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = vk->surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };
    
    VkResult result = vkCreateSwapchainKHR(vk->device, &swapchain_create_info, NULL, &vk->swapchain);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create swap chain: %d", result);
        return -1;
    }
    
    // 获取交换链图像
    vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->image_count, NULL);
    vk->images = (VkImage*)malloc(sizeof(VkImage) * vk->image_count);
    vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->image_count, vk->images);
    
    // 创建图像视图
    vk->image_views = (VkImageView*)malloc(sizeof(VkImageView) * vk->image_count);
    
    for (uint32_t i = 0; i < vk->image_count; i++) {
        VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .image = vk->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surface_format.format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        
        result = vkCreateImageView(vk->device, &create_info, NULL, &vk->image_views[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create image view %u: %d", i, result);
            return -1;
        }
    }
    
    return 0;
}

// 创建渲染通道
static int create_render_pass(struct vulkan_state* vk) {
    VkAttachmentDescription color_attachment = {
        .flags = 0,
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    
    VkAttachmentReference color_attachment_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    
    VkSubpassDescription subpass = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = NULL,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
        .pResolveAttachments = NULL,
        .pDepthStencilAttachment = NULL,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = NULL
    };
    
    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0
    };
    
    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };
    
    VkResult result = vkCreateRenderPass(vk->device, &render_pass_info, NULL, &vk->render_pass);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create render pass: %d", result);
        return -1;
    }
    
    return 0;
}

// 创建帧缓冲区
static int create_framebuffers(struct vulkan_state* vk) {
    vk->framebuffers = (VkFramebuffer*)malloc(sizeof(VkFramebuffer) * vk->image_count);
    
    for (uint32_t i = 0; i < vk->image_count; i++) {
        VkImageView attachments[] = {
            vk->image_views[i]
        };
        
        VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .renderPass = vk->render_pass,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = vk->width,
            .height = vk->height,
            .layers = 1
        };
        
        VkResult result = vkCreateFramebuffer(vk->device, &framebuffer_info, NULL, &vk->framebuffers[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create framebuffer %u: %d", i, result);
            return -1;
        }
    }
    
    return 0;
}

// 创建命令池
static int create_command_pool(struct vulkan_state* vk) {
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk->queue_family_index
    };
    
    VkResult result = vkCreateCommandPool(vk->device, &pool_info, NULL, &vk->command_pool);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create command pool: %d", result);
        return -1;
    }
    
    return 0;
}

// 创建命令缓冲区
static int create_command_buffers(struct vulkan_state* vk) {
    vk->command_buffers = (VkCommandBuffer*)malloc(sizeof(VkCommandBuffer) * vk->image_count);
    
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = vk->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = vk->image_count
    };
    
    VkResult result = vkAllocateCommandBuffers(vk->device, &alloc_info, vk->command_buffers);
    if (result != VK_SUCCESS) {
        LOGE("Failed to allocate command buffers: %d", result);
        return -1;
    }
    
    return 0;
}

// 创建同步对象
static int create_sync_objects(struct vulkan_state* vk) {
    vk->image_available_semaphores = (VkSemaphore*)malloc(sizeof(VkSemaphore) * vk->image_count);
    vk->render_finished_semaphores = (VkSemaphore*)malloc(sizeof(VkSemaphore) * vk->image_count);
    vk->in_flight_fences = (VkFence*)malloc(sizeof(VkFence) * vk->image_count);
    
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0
    };
    
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    
    for (size_t i = 0; i < vk->image_count; i++) {
        VkResult result = vkCreateSemaphore(vk->device, &semaphore_info, NULL, &vk->image_available_semaphores[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create image available semaphore %zu: %d", i, result);
            return -1;
        }
        
        result = vkCreateSemaphore(vk->device, &semaphore_info, NULL, &vk->render_finished_semaphores[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create render finished semaphore %zu: %d", i, result);
            return -1;
        }
        
        result = vkCreateFence(vk->device, &fence_info, NULL, &vk->in_flight_fences[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create fence %zu: %d", i, result);
            return -1;
        }
    }
    
    return 0;
}

// 初始化Vulkan
int vulkan_init(struct vulkan_state* vk, ANativeWindow* window, int width, int height) {
    if (!vk || !window || width <= 0 || height <= 0) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    memset(vk, 0, sizeof(struct vulkan_state));
    vk->width = width;
    vk->height = height;
    
    // 创建Vulkan实例
    if (create_vulkan_instance(vk) != 0) {
        LOGE("Failed to create Vulkan instance");
        return -1;
    }
    
    // 创建Android表面
    if (create_android_surface(vk, window) != 0) {
        LOGE("Failed to create Android surface");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 选择物理设备并创建逻辑设备
    if (pick_physical_device(vk) != 0) {
        LOGE("Failed to pick physical device");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 创建交换链
    if (create_swapchain(vk) != 0) {
        LOGE("Failed to create swap chain");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 创建渲染通道
    if (create_render_pass(vk) != 0) {
        LOGE("Failed to create render pass");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 创建帧缓冲区
    if (create_framebuffers(vk) != 0) {
        LOGE("Failed to create framebuffers");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 创建命令池
    if (create_command_pool(vk) != 0) {
        LOGE("Failed to create command pool");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 创建命令缓冲区
    if (create_command_buffers(vk) != 0) {
        LOGE("Failed to create command buffers");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 创建同步对象
    if (create_sync_objects(vk) != 0) {
        LOGE("Failed to create sync objects");
        vulkan_destroy(vk);
        return -1;
    }
    
    vk->initialized = true;
    LOGI("Vulkan initialized successfully");
    return 0;
}

// 销毁Vulkan
void vulkan_destroy(struct vulkan_state* vk) {
    if (!vk || !vk->initialized) {
        return;
    }
    
    vkDeviceWaitIdle(vk->device);
    
    // 清理同步对象
    for (uint32_t i = 0; i < vk->image_count; i++) {
        if (vk->image_available_semaphores && vk->image_available_semaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(vk->device, vk->image_available_semaphores[i], NULL);
        }
        if (vk->render_finished_semaphores && vk->render_finished_semaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(vk->device, vk->render_finished_semaphores[i], NULL);
        }
        if (vk->in_flight_fences && vk->in_flight_fences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(vk->device, vk->in_flight_fences[i], NULL);
        }
    }
    
    free(vk->image_available_semaphores);
    free(vk->render_finished_semaphores);
    free(vk->in_flight_fences);
    
    // 清理命令池
    if (vk->command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vk->device, vk->command_pool, NULL);
    }
    
    free(vk->command_buffers);
    
    // 清理帧缓冲区
    for (uint32_t i = 0; i < vk->image_count; i++) {
        if (vk->framebuffers && vk->framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(vk->device, vk->framebuffers[i], NULL);
        }
    }
    
    free(vk->framebuffers);
    
    // 清理渲染通道
    if (vk->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vk->device, vk->render_pass, NULL);
    }
    
    // 清理图像视图
    for (uint32_t i = 0; i < vk->image_count; i++) {
        if (vk->image_views && vk->image_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(vk->device, vk->image_views[i], NULL);
        }
    }
    
    free(vk->image_views);
    free(vk->images);
    
    // 清理交换链
    if (vk->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vk->device, vk->swapchain, NULL);
    }
    
    // 清理设备
    if (vk->device != VK_NULL_HANDLE) {
        vkDestroyDevice(vk->device, NULL);
    }
    
    // 清理表面
    if (vk->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(vk->instance, vk->surface, NULL);
    }
    
    // 清理实例
    if (vk->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(vk->instance, NULL);
    }
    
    memset(vk, 0, sizeof(struct vulkan_state));
    LOGI("Vulkan destroyed");
}

// 重建交换链
int vulkan_recreate_swapchain(struct vulkan_state* vk, int width, int height) {
    if (!vk || !vk->initialized || width <= 0 || height <= 0) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    vkDeviceWaitIdle(vk->device);
    
    // 清理旧的交换链相关资源
    for (uint32_t i = 0; i < vk->image_count; i++) {
        vkDestroyFramebuffer(vk->device, vk->framebuffers[i], NULL);
        vkDestroyImageView(vk->device, vk->image_views[i], NULL);
    }
    
    free(vk->framebuffers);
    free(vk->image_views);
    free(vk->images);
    
    vkDestroySwapchainKHR(vk->device, vk->swapchain, NULL);
    
    // 更新尺寸
    vk->width = width;
    vk->height = height;
    
    // 重新创建交换链
    if (create_swapchain(vk) != 0) {
        LOGE("Failed to recreate swap chain");
        return -1;
    }
    
    // 重新创建帧缓冲区
    if (create_framebuffers(vk) != 0) {
        LOGE("Failed to recreate framebuffers");
        return -1;
    }
    
    LOGI("Swap chain recreated with size %dx%d", width, height);
    return 0;
}

// 开始渲染帧
int vulkan_begin_frame(struct vulkan_state* vk, uint32_t* image_index) {
    if (!vk || !vk->initialized || !image_index) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    // 等待上一帧完成
    vkWaitForFences(vk->device, 1, &vk->in_flight_fences[vk->current_image], VK_TRUE, UINT64_MAX);
    vkResetFences(vk->device, 1, &vk->in_flight_fences[vk->current_image]);
    
    // 获取下一个图像
    VkResult result = vkAcquireNextImageKHR(vk->device, vk->swapchain, UINT64_MAX, 
                                           vk->image_available_semaphores[vk->current_image], 
                                           VK_NULL_HANDLE, image_index);
    
    if (result != VK_SUCCESS) {
        LOGE("Failed to acquire next image: %d", result);
        return -1;
    }
    
    // 重置命令缓冲区
    vkResetCommandBuffer(vk->command_buffers[*image_index], 0);
    
    return 0;
}

// 结束渲染帧
int vulkan_end_frame(struct vulkan_state* vk, uint32_t image_index) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    // 提交命令缓冲区
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vk->image_available_semaphores[image_index],
        .pWaitDstStageMask = &(VkPipelineStageFlags){VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
        .commandBufferCount = 1,
        .pCommandBuffers = &vk->command_buffers[image_index],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &vk->render_finished_semaphores[image_index]
    };
    
    VkResult result = vkQueueSubmit(vk->queue, 1, &submit_info, vk->in_flight_fences[image_index]);
    if (result != VK_SUCCESS) {
        LOGE("Failed to submit command buffer: %d", result);
        return -1;
    }
    
    // 呈现
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vk->render_finished_semaphores[image_index],
        .swapchainCount = 1,
        .pSwapchains = &vk->swapchain,
        .pImageIndices = &image_index,
        .pResults = NULL
    };
    
    result = vkQueuePresentKHR(vk->queue, &present_info);
    if (result != VK_SUCCESS) {
        LOGE("Failed to present queue: %d", result);
        return -1;
    }
    
    vk->current_image = (vk->current_image + 1) % vk->image_count;
    
    return 0;
}

// 获取当前帧缓冲区
VkFramebuffer vulkan_get_current_framebuffer(struct vulkan_state* vk, uint32_t image_index) {
    if (!vk || !vk->initialized || image_index >= vk->image_count) {
        return VK_NULL_HANDLE;
    }
    
    return vk->framebuffers[image_index];
}

// 获取命令缓冲区
VkCommandBuffer vulkan_get_command_buffer(struct vulkan_state* vk, uint32_t image_index) {
    if (!vk || !vk->initialized || image_index >= vk->image_count) {
        return VK_NULL_HANDLE;
    }
    
    return vk->command_buffers[image_index];
}