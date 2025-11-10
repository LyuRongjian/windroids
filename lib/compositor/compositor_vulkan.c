#include "compositor_vulkan.h"
#include "memory_pool.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "android/log.h"

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

// 初始化多线程渲染器
// 注意：实际实现在第524行




    
    




            free(renderer->threads);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            return -1;
        }
        
        // 创建栅栏
        VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0
        };
        
        result = vkCreateFence(vk->device, &fence_info, NULL, &thread_data->fence);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create fence for thread %u: %d", i, result);
            
            // 清理已创建的资源
            for (uint32_t j = 0; j < i; j++) {
                vkDestroyCommandPool(vk->device, renderer->threads[j].command_pool, NULL);
                vkDestroySemaphore(vk->device, renderer->threads[j].semaphore, NULL);
                vkDestroyFence(vk->device, renderer->threads[j].fence, NULL);
            }
            vkDestroyCommandPool(vk->device, thread_data->command_pool, NULL);
            vkDestroySemaphore(vk->device, thread_data->semaphore, NULL);
            free(renderer->threads);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            return -1;
        }
    }
    
    renderer->thread_count = thread_count;
    renderer->enabled = true;
    renderer->has_work = false;
    renderer->work_done = false;
    
    // 创建渲染线程
    for (uint32_t i = 0; i < thread_count; i++) {
        if (pthread_create(&renderer->threads[i].thread_id, NULL, render_thread_func, &renderer->threads[i]) != 0) {
            LOGE("Failed to create render thread %u", i);
            
            // 设置退出标志并等待已创建的线程退出
            for (uint32_t j = 0; j < i; j++) {
                renderer->threads[j].should_exit = true;
                pthread_cond_signal(&renderer->condition);
                pthread_join(renderer->threads[j].thread_id, NULL);
            }
            
            // 清理资源
            for (uint32_t j = 0; j < thread_count; j++) {
                vkDestroyCommandPool(vk->device, renderer->threads[j].command_pool, NULL);
                vkDestroySemaphore(vk->device, renderer->threads[j].semaphore, NULL);
                vkDestroyFence(vk->device, renderer->threads[j].fence, NULL);
            }
            free(renderer->threads);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            return -1;
        }
    }
    
    LOGI("Multithread renderer initialized with %u threads", thread_count);
    return 0;
}

// 销毁多线程渲染器
void vulkan_destroy_multithread_renderer(struct vulkan_state* vk) {
    if (!vk || !vk->multithread_renderer.enabled) {
        return;
    }
    
    struct multithread_renderer* renderer = &vk->multithread_renderer;
    
    // 设置退出标志并通知所有线程
    pthread_mutex_lock(&renderer->mutex);
    renderer->enabled = false;
    for (uint32_t i = 0; i < renderer->thread_count; i++) {
        renderer->threads[i].should_exit = true;
    }
    pthread_cond_broadcast(&renderer->condition);
    pthread_mutex_unlock(&renderer->mutex);
    
    // 等待所有线程退出
    for (uint32_t i = 0; i < renderer->thread_count; i++) {
        pthread_join(renderer->threads[i].thread_id, NULL);
    }
    
    // 清理资源
    for (uint32_t i = 0; i < renderer->thread_count; i++) {
        struct render_thread_data* thread_data = &renderer->threads[i];
        
        if (thread_data->command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vk->device, thread_data->command_pool, NULL);
        }
        
        if (thread_data->semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(vk->device, thread_data->semaphore, NULL);
        }
        
        if (thread_data->fence != VK_NULL_HANDLE) {
            vkDestroyFence(vk->device, thread_data->fence, NULL);
        }
    }
    
    free(renderer->threads);
    
    // 销毁同步对象
    pthread_mutex_destroy(&renderer->mutex);
    pthread_cond_destroy(&renderer->condition);
    
    memset(renderer, 0, sizeof(struct multithread_renderer));
    
    LOGI("Multithread renderer destroyed");
}





// 初始化Android特定优化
int vulkan_init_android_optimizations(struct vulkan_state* vk) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    // 初始化Android优化结构
    memset(&vk->android_opts, 0, sizeof(struct android_optimizations));
    
    // 默认启用一些Android特定优化
    vk->android_opts.use_external_memory = true;
    vk->android_opts.use_android_hardware_buffer = true;
    vk->android_opts.use_android_sync = true;
    vk->android_opts.use_surface_rotation = true;
    vk->android_opts.performance_level = 2; // 中等性能级别
    vk->android_opts.thermal_throttling_factor = 1.0f;
    
    // 检查设备是否支持外部内存
    VkPhysicalDeviceExternalMemoryHostPropertiesKHR ext_mem_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_KHR
    };
    
    VkPhysicalDeviceProperties2KHR props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
        .pNext = &ext_mem_props
    };
    
    // 检查设备是否支持Android硬件缓冲区
    VkPhysicalDeviceExternalBufferInfoKHR ext_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO_KHR,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID
    };
    
    VkExternalBufferPropertiesKHR ext_buffer_props = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES_KHR
    };
    
    // 应用Android优化
    if (vulkan_apply_android_optimizations(vk) != 0) {
        LOGE("Failed to apply Android optimizations");
        return -1;
    }
    
    LOGI("Android optimizations initialized");
    return 0;
}

// 销毁Android特定优化
void vulkan_destroy_android_optimizations(struct vulkan_state* vk) {
    if (!vk || !vk->initialized) {
        return;
    }
    
    // 清理Android优化相关的资源
    memset(&vk->android_opts, 0, sizeof(struct android_optimizations));
    
    LOGI("Android optimizations destroyed");
}

// 应用Android特定优化
int vulkan_apply_android_optimizations(struct vulkan_state* vk) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    // 根据性能级别调整渲染参数
    switch (vk->android_opts.performance_level) {
        case 0: // 省电模式
            vk->android_opts.use_low_latency_mode = false;
            vk->android_opts.use_power_saving_mode = true;
            vk->android_opts.use_async_compute = false;
            break;
        case 1: // 平衡模式
            vk->android_opts.use_low_latency_mode = false;
            vk->android_opts.use_power_saving_mode = false;
            vk->android_opts.use_async_compute = false;
            break;
        case 2: // 性能模式
            vk->android_opts.use_low_latency_mode = true;
            vk->android_opts.use_power_saving_mode = false;
            vk->android_opts.use_async_compute = true;
            break;
        case 3: // 极限性能模式
            vk->android_opts.use_low_latency_mode = true;
            vk->android_opts.use_power_saving_mode = false;
            vk->android_opts.use_async_compute = true;
            vk->android_opts.thermal_throttling_factor = 0.8f; // 允许更高的温度
            break;
    }
    
    // 应用热节流因子
    if (vk->android_opts.thermal_throttling_factor < 0.5f) {
        vk->android_opts.thermal_throttling_factor = 0.5f;
    } else if (vk->android_opts.thermal_throttling_factor > 1.0f) {
        vk->android_opts.thermal_throttling_factor = 1.0f;
    }
    
    LOGI("Android optimizations applied: performance level=%d, low_latency=%d, power_saving=%d, async_compute=%d",
         vk->android_opts.performance_level,
         vk->android_opts.use_low_latency_mode,
         vk->android_opts.use_power_saving_mode,
         vk->android_opts.use_async_compute);
    
    return 0;
}

// 更新Android性能级别
int vulkan_update_android_performance_level(struct vulkan_state* vk, uint32_t level) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    if (level > 3) {
        LOGE("Invalid performance level: %d (must be 0-3)", level);
        return -1;
    }
    
    uint32_t old_level = vk->android_opts.performance_level;
    vk->android_opts.performance_level = level;
    
    // 应用新的性能级别
    if (vulkan_apply_android_optimizations(vk) != 0) {
        LOGE("Failed to apply new performance level");
        vk->android_opts.performance_level = old_level; // 恢复旧级别
        return -1;
    }
    
    LOGI("Android performance level updated from %d to %d", old_level, level);
    return 0;
}

// 设置Android表面变换
int vulkan_set_android_surface_transform(struct vulkan_state* vk, uint32_t transform) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    vk->android_opts.surface_transform = transform;
    LOGI("Android surface transform set to %d", transform);
    return 0;
}

// 启用/禁用Android硬件缓冲区
int vulkan_enable_android_hardware_buffer(struct vulkan_state* vk, bool enable) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    vk->android_opts.use_android_hardware_buffer = enable;
    LOGI("Android hardware buffer %s", enable ? "enabled" : "disabled");
    return 0;
}

// 启用/禁用Android外部内存
int vulkan_enable_android_external_memory(struct vulkan_state* vk, bool enable) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    vk->android_opts.use_external_memory = enable;
    LOGI("Android external memory %s", enable ? "enabled" : "disabled");
    return 0;
}

// 更新热节流因子
int vulkan_update_thermal_throttling(struct vulkan_state* vk, float factor) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    if (factor < 0.5f || factor > 1.0f) {
        LOGE("Invalid thermal throttling factor: %.2f (must be 0.5-1.0)", factor);
        return -1;
    }
    
    vk->android_opts.thermal_throttling_factor = factor;
    LOGI("Thermal throttling factor updated to %.2f", factor);
    return 0;
}



// 初始化多线程渲染管理器
int vulkan_init_multithread_renderer(struct vulkan_state* vk, uint32_t thread_count) {
    if (!vk || thread_count == 0) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    struct multithread_renderer* renderer = &vk->multithread_renderer;
    
    // 如果已经初始化，先销毁
    if (renderer->enabled) {
        vulkan_destroy_multithread_renderer(vk);
    }
    
    // 设置线程数
    renderer->thread_count = thread_count;
    
    // 分配线程数据数组
    renderer->threads = (struct render_thread_data*)compositor_memory_alloc(sizeof(struct render_thread_data) * thread_count);
    if (!renderer->threads) {
        LOGE("Failed to allocate threads data");
        return -1;
    }
    
    // 初始化互斥锁和条件变量
    if (pthread_mutex_init(&renderer->mutex, NULL) != 0) {
        LOGE("Failed to initialize mutex");
        compositor_memory_free(renderer->threads);
        compositor_memory_free(renderer->threads_data);
        return -1;
    }
    
    if (pthread_cond_init(&renderer->condition, NULL) != 0) {
        LOGE("Failed to initialize condition variable");
        pthread_mutex_destroy(&renderer->mutex);
        compositor_memory_free(renderer->threads);
        compositor_memory_free(renderer->threads_data);
        return -1;
    }
    
    // 初始化工作状态
    renderer->has_work = false;
    renderer->work_done = false;
    
    // 创建渲染完成信号量
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0
    };
    
    VkResult result = vkCreateSemaphore(vk->device, &semaphore_info, NULL, &renderer->render_complete_semaphore);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create render complete semaphore: %d", result);
        pthread_mutex_destroy(&renderer->mutex);
        pthread_cond_destroy(&renderer->condition);
        compositor_memory_free(renderer->threads);
        compositor_memory_free(renderer->threads_data);
        return -1;
    }
    
    // 初始化每个线程
    for (uint32_t i = 0; i < thread_count; i++) {
        struct render_thread_data* thread_data = &renderer->threads[i];
        
        // 设置线程数据
        thread_data->vk = vk;
        thread_data->thread_index = i;
        thread_data->should_exit = false;
        thread_data->is_ready = false;
        thread_data->active = true;
        
        // 初始化线程互斥锁和条件变量
        if (pthread_mutex_init(&thread_data->mutex, NULL) != 0) {
            LOGE("Failed to initialize thread %u mutex", i);
            // 清理已创建的资源
            for (uint32_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&renderer->threads[j].mutex);
                pthread_cond_destroy(&renderer->threads[j].condition);
            }
            vkDestroySemaphore(vk->device, renderer->render_complete_semaphore, NULL);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            compositor_memory_free(renderer->threads);
            return -1;
        }
        
        if (pthread_cond_init(&thread_data->condition, NULL) != 0) {
            LOGE("Failed to initialize thread %u condition", i);
            pthread_mutex_destroy(&thread_data->mutex);
            // 清理已创建的资源
            for (uint32_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&renderer->threads[j].mutex);
                pthread_cond_destroy(&renderer->threads[j].condition);
            }
            vkDestroySemaphore(vk->device, renderer->render_complete_semaphore, NULL);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            compositor_memory_free(renderer->threads);
            return -1;
        }
        
        // 创建命令池
        VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = vk->queue_family_index
        };
        
        result = vkCreateCommandPool(vk->device, &pool_info, NULL, &thread_data->command_pool);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create command pool for thread %u: %d", i, result);
            pthread_mutex_destroy(&thread_data->mutex);
            pthread_cond_destroy(&thread_data->condition);
            // 清理已创建的资源
            for (uint32_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&renderer->threads[j].mutex);
                pthread_cond_destroy(&renderer->threads[j].condition);
                vkDestroyCommandPool(vk->device, renderer->threads[j].command_pool, NULL);
            }
            vkDestroySemaphore(vk->device, renderer->render_complete_semaphore, NULL);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            compositor_memory_free(renderer->threads);
            return -1;
        }
        
        // 分配命令缓冲区
        VkCommandBufferAllocateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = NULL,
            .commandPool = thread_data->command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        
        result = vkAllocateCommandBuffers(vk->device, &buffer_info, &thread_data->command_buffer);
        if (result != VK_SUCCESS) {
            LOGE("Failed to allocate command buffer for thread %u: %d", i, result);
            vkDestroyCommandPool(vk->device, thread_data->command_pool, NULL);
            pthread_mutex_destroy(&thread_data->mutex);
            pthread_cond_destroy(&thread_data->condition);
            // 清理已创建的资源
            for (uint32_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&renderer->threads[j].mutex);
                pthread_cond_destroy(&renderer->threads[j].condition);
                vkDestroyCommandPool(vk->device, renderer->threads[j].command_pool, NULL);
            }
            vkDestroySemaphore(vk->device, renderer->render_complete_semaphore, NULL);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            compositor_memory_free(renderer->threads);
            return -1;
        }
        
        // 创建信号量
        VkSemaphoreCreateInfo semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0
        };
        
        result = vkCreateSemaphore(vk->device, &semaphore_info, NULL, &thread_data->semaphore);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create semaphore for thread %u: %d", i, result);
            vkFreeCommandBuffers(vk->device, thread_data->command_pool, 1, &thread_data->command_buffer);
            vkDestroyCommandPool(vk->device, thread_data->command_pool, NULL);
            pthread_mutex_destroy(&thread_data->mutex);
            pthread_cond_destroy(&thread_data->condition);
            // 清理已创建的资源
            for (uint32_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&renderer->threads[j].mutex);
                pthread_cond_destroy(&renderer->threads[j].condition);
                vkDestroyCommandPool(vk->device, renderer->threads[j].command_pool, NULL);
                vkDestroySemaphore(vk->device, renderer->threads[j].semaphore, NULL);
                vkFreeCommandBuffers(vk->device, renderer->threads[j].command_pool, 1, &renderer->threads[j].command_buffer);
            }
            vkDestroySemaphore(vk->device, renderer->render_complete_semaphore, NULL);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            compositor_memory_free(renderer->threads);
            return -1;
        }
        
        // 创建栅栏
        VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = NULL,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };
        
        result = vkCreateFence(vk->device, &fence_info, NULL, &thread_data->fence);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create fence for thread %u: %d", i, result);
            vkDestroySemaphore(vk->device, thread_data->semaphore, NULL);
            vkFreeCommandBuffers(vk->device, thread_data->command_pool, 1, &thread_data->command_buffer);
            vkDestroyCommandPool(vk->device, thread_data->command_pool, NULL);
            pthread_mutex_destroy(&thread_data->mutex);
            pthread_cond_destroy(&thread_data->condition);
            // 清理已创建的资源
            for (uint32_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&renderer->threads[j].mutex);
                pthread_cond_destroy(&renderer->threads[j].condition);
                vkDestroyCommandPool(vk->device, renderer->threads[j].command_pool, NULL);
                vkDestroySemaphore(vk->device, renderer->threads[j].semaphore, NULL);
                vkFreeCommandBuffers(vk->device, renderer->threads[j].command_pool, 1, &renderer->threads[j].command_buffer);
                vkDestroyFence(vk->device, renderer->threads[j].fence, NULL);
            }
            vkDestroySemaphore(vk->device, renderer->render_complete_semaphore, NULL);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            compositor_memory_free(renderer->threads);
            return -1;
        }
        
        // 初始化工作状态
        thread_data->has_work = false;
        thread_data->should_exit = false;
        thread_data->thread_index = i;
        thread_data->vk = vk;
        
        // 创建线程
        if (pthread_create(&thread_data->thread_id, NULL, render_thread_func, thread_data) != 0) {
            LOGE("Failed to create thread %u", i);
            vkDestroyFence(vk->device, thread_data->fence, NULL);
            vkDestroySemaphore(vk->device, thread_data->semaphore, NULL);
            vkFreeCommandBuffers(vk->device, thread_data->command_pool, 1, &thread_data->command_buffer);
            vkDestroyCommandPool(vk->device, thread_data->command_pool, NULL);
            pthread_mutex_destroy(&thread_data->mutex);
            pthread_cond_destroy(&thread_data->condition);
            // 清理已创建的资源
            for (uint32_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&renderer->threads[j].mutex);
                pthread_cond_destroy(&renderer->threads[j].condition);
                vkDestroyCommandPool(vk->device, renderer->threads[j].command_pool, NULL);
                vkDestroySemaphore(vk->device, renderer->threads[j].semaphore, NULL);
                vkFreeCommandBuffers(vk->device, renderer->threads[j].command_pool, 1, &renderer->threads[j].command_buffer);
                vkDestroyFence(vk->device, renderer->threads[j].fence, NULL);
            }
            vkDestroySemaphore(vk->device, renderer->render_complete_semaphore, NULL);
            pthread_mutex_destroy(&renderer->mutex);
            pthread_cond_destroy(&renderer->condition);
            compositor_memory_free(renderer->threads);
            return -1;
        }
    }
    
    // 等待所有线程准备就绪
    for (uint32_t i = 0; i < thread_count; i++) {
        while (!renderer->threads[i].is_ready) {
            usleep(1000); // 1ms
        }
    }
    
    // 设置为已启用
    renderer->enabled = true;
    renderer->initialized = true;
    
    LOGI("Multithread renderer initialized with %u threads", thread_count);
    return 0;
}

// 销毁多线程渲染管理器
void vulkan_destroy_multithread_renderer(struct vulkan_state* vk) {
    if (!vk || !vk->multithread_renderer.initialized) {
        return;
    }
    
    struct multithread_renderer* renderer = &vk->multithread_renderer;
    
    // 停止所有线程
    for (uint32_t i = 0; i < renderer->thread_count; i++) {
        renderer->threads[i].should_exit = true;
        pthread_cond_signal(&renderer->threads[i].condition);
        pthread_join(renderer->threads[i].thread_id, NULL);
        
        // 清理线程同步对象
        pthread_mutex_destroy(&renderer->threads[i].mutex);
        pthread_cond_destroy(&renderer->threads[i].condition);
        vkDestroyCommandPool(vk->device, renderer->threads[i].command_pool, NULL);
        vkDestroySemaphore(vk->device, renderer->threads[i].semaphore, NULL);
        vkDestroyFence(vk->device, renderer->threads[i].fence, NULL);
    }
    
    // 销毁信号量
    if (renderer->render_complete_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(vk->device, renderer->render_complete_semaphore, NULL);
        renderer->render_complete_semaphore = VK_NULL_HANDLE;
    }
    
    // 释放线程资源
    compositor_memory_free(renderer->threads);
    
    memset(renderer, 0, sizeof(struct multithread_renderer));
    
    LOGI("Multithread renderer destroyed");
}

// 多线程渲染
int vulkan_render_multithread(struct vulkan_state* vk, render_task_func_t render_func, void* user_data, 
                              uint32_t task_count) {
    if (!vk || !vk->multithread_renderer.initialized || !render_func || task_count == 0) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    struct multithread_renderer* renderer = &vk->multithread_renderer;
    
    // 分配任务给各个线程
    uint32_t tasks_per_thread = task_count / renderer->thread_count;
    uint32_t remaining_tasks = task_count % renderer->thread_count;
    
    for (uint32_t i = 0; i < renderer->thread_count; i++) {
        struct render_thread_data* thread_data = &renderer->threads[i];
        
        // 准备任务
        thread_data->task.render_func = render_func;
        thread_data->task.user_data = user_data;
        thread_data->task.task_id = i;
        thread_data->task.start_task = i * tasks_per_thread;
        thread_data->task.end_task = thread_data->task.start_task + tasks_per_thread;
        
        // 分配剩余任务
        if (i < remaining_tasks) {
            thread_data->task.end_task++;
        }
        
        // 通知线程有新任务
        pthread_mutex_lock(&thread_data->mutex);
        thread_data->has_work = true;
        pthread_cond_signal(&thread_data->condition);
        pthread_mutex_unlock(&thread_data->mutex);
    }
    
    // 等待所有线程完成
    for (uint32_t i = 0; i < renderer->thread_count; i++) {
        pthread_mutex_lock(&renderer->threads[i].mutex);
        while (renderer->threads[i].has_work) {
            pthread_cond_wait(&renderer->threads[i].condition, &renderer->threads[i].mutex);
        }
        pthread_mutex_unlock(&renderer->threads[i].mutex);
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
    
    // 保存物理设备到状态结构
    vk->physical_device = physical_device;
    
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
    
    // 初始化命令缓冲区缓存（创建4个次要命令缓冲区用于预记录内容）
    if (vulkan_init_command_buffer_cache(vk, 4) != 0) {
        LOGE("Failed to initialize command buffer cache");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 初始化批处理管理器
    if (vulkan_init_batch_manager(vk, 100, 10000, 20000) != 0) {
        LOGE("Failed to initialize batch manager");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 初始化多线程渲染管理器
    if (vulkan_init_multithread_renderer(vk, 2) != 0) {
        LOGE("Failed to initialize multithread renderer");
        vulkan_destroy(vk);
        return -1;
    }
    
    // 初始化Android特定优化
    if (vulkan_init_android_optimizations(vk) != 0) {
        LOGE("Failed to initialize Android optimizations");
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
    
    // 销毁Android特定优化
    vulkan_destroy_android_optimizations(vk);
    
    // 销毁多线程渲染管理器
    vulkan_destroy_multithread_renderer(vk);
    
    // 销毁批处理管理器
    vulkan_destroy_batch_manager(vk);
    
    // 销毁命令缓冲区缓存
    vulkan_destroy_command_buffer_cache(vk);
    
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
    
    // 重新初始化批处理管理器
    vulkan_destroy_batch_manager(vk);
    if (vulkan_init_batch_manager(vk, 100, 10000, 20000) != 0) {
        LOGE("Failed to reinitialize batch manager");
        return -1;
    }
    
    // 重新初始化多线程渲染管理器
    vulkan_destroy_multithread_renderer(vk);
    if (vulkan_init_multithread_renderer(vk, 2) != 0) {
        LOGE("Failed to reinitialize multithread renderer");
        return -1;
    }
    
    // 标记所有命令缓冲区为脏，因为帧缓冲区已改变
    if (vk->use_command_buffer_cache) {
        for (uint32_t i = 0; i < vk->image_count; i++) {
            vk->primary_cache[i].is_dirty = true;
            vk->primary_cache[i].is_recorded = false;
        }
        
        for (uint32_t i = 0; i < vk->secondary_buffer_count; i++) {
            vk->secondary_cache[i].is_dirty = true;
            vk->secondary_cache[i].is_recorded = false;
        }
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
    
    // 开始新的批处理
    vulkan_begin_batch(vk);
    
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
    
    // 检查是否需要重置命令缓冲区
    if (!vk->use_command_buffer_cache || vk->primary_cache[*image_index].is_dirty) {
        // 重置命令缓冲区
        vkResetCommandBuffer(vk->command_buffers[*image_index], 0);
        
        // 更新缓存状态
        if (vk->use_command_buffer_cache) {
            vk->primary_cache[*image_index].is_recorded = false;
            vk->primary_cache[*image_index].is_dirty = false;
        }
    }
    
    return 0;
}

// 结束渲染帧
int vulkan_end_frame(struct vulkan_state* vk, uint32_t image_index) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    // 结束当前批处理
    vulkan_end_batch(vk);
    
    // 使用多线程渲染批处理
    if (vk->multithread_renderer.enabled) {
        vulkan_render_multithread(vk, vk->command_buffers[image_index]);
    } else {
        // 单线程渲染批处理
        vulkan_render_batches(vk, vk->command_buffers[image_index]);
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
    
    // 更新帧计数
    vulkan_update_frame_counter(vk);
    
    return 0;
}

// 初始化命令缓冲区缓存
int vulkan_init_command_buffer_cache(struct vulkan_state* vk, uint32_t secondary_buffer_count) {
    if (!vk || !vk->initialized) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    // 分配主要命令缓冲区缓存
    vk->primary_cache = (struct command_buffer_cache*)malloc(sizeof(struct command_buffer_cache) * vk->image_count);
    if (!vk->primary_cache) {
        LOGE("Failed to allocate primary command buffer cache");
        return -1;
    }
    
    // 初始化主要命令缓冲区缓存
    for (uint32_t i = 0; i < vk->image_count; i++) {
        vk->primary_cache[i].command_buffer = vk->command_buffers[i];
        vk->primary_cache[i].is_recorded = false;
        vk->primary_cache[i].is_dirty = true;
        vk->primary_cache[i].last_used_frame = 0;
    }
    
    // 分配次要命令缓冲区缓存
    vk->secondary_buffer_count = secondary_buffer_count;
    vk->secondary_cache = (struct command_buffer_cache*)malloc(sizeof(struct command_buffer_cache) * secondary_buffer_count);
    if (!vk->secondary_cache) {
        LOGE("Failed to allocate secondary command buffer cache");
        free(vk->primary_cache);
        vk->primary_cache = NULL;
        return -1;
    }
    
    // 创建次要命令缓冲区
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = vk->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
        .commandBufferCount = secondary_buffer_count
    };
    
    VkCommandBuffer* secondary_buffers = (VkCommandBuffer*)malloc(sizeof(VkCommandBuffer) * secondary_buffer_count);
    if (!secondary_buffers) {
        LOGE("Failed to allocate secondary command buffers");
        free(vk->primary_cache);
        vk->primary_cache = NULL;
        free(vk->secondary_cache);
        vk->secondary_cache = NULL;
        return -1;
    }
    
    VkResult result = vkAllocateCommandBuffers(vk->device, &alloc_info, secondary_buffers);
    if (result != VK_SUCCESS) {
        LOGE("Failed to allocate secondary command buffers: %d", result);
        free(vk->primary_cache);
        vk->primary_cache = NULL;
        free(vk->secondary_cache);
        vk->secondary_cache = NULL;
        free(secondary_buffers);
        return -1;
    }
    
    // 初始化次要命令缓冲区缓存
    for (uint32_t i = 0; i < secondary_buffer_count; i++) {
        vk->secondary_cache[i].command_buffer = secondary_buffers[i];
        vk->secondary_cache[i].is_recorded = false;
        vk->secondary_cache[i].is_dirty = true;
        vk->secondary_cache[i].last_used_frame = 0;
    }
    
    free(secondary_buffers);
    
    // 初始化其他状态
    vk->current_frame = 0;
    vk->use_command_buffer_cache = true;
    
    LOGI("Command buffer cache initialized with %u primary and %u secondary buffers", 
         vk->image_count, secondary_buffer_count);
    
    return 0;
}

// 销毁命令缓冲区缓存
void vulkan_destroy_command_buffer_cache(struct vulkan_state* vk) {
    if (!vk || !vk->initialized) {
        return;
    }
    
    // 释放次要命令缓冲区
    if (vk->secondary_cache) {
        VkCommandBuffer* secondary_buffers = (VkCommandBuffer*)malloc(sizeof(VkCommandBuffer) * vk->secondary_buffer_count);
        if (secondary_buffers) {
            for (uint32_t i = 0; i < vk->secondary_buffer_count; i++) {
                secondary_buffers[i] = vk->secondary_cache[i].command_buffer;
            }
            
            vkFreeCommandBuffers(vk->device, vk->command_pool, vk->secondary_buffer_count, secondary_buffers);
            free(secondary_buffers);
        }
        
        free(vk->secondary_cache);
        vk->secondary_cache = NULL;
    }
    
    // 释放主要命令缓冲区缓存
    if (vk->primary_cache) {
        free(vk->primary_cache);
        vk->primary_cache = NULL;
    }
    
    vk->use_command_buffer_cache = false;
    
    LOGI("Command buffer cache destroyed");
}

// 获取缓存的命令缓冲区
VkCommandBuffer vulkan_get_cached_command_buffer(struct vulkan_state* vk, command_buffer_type_t type, uint32_t index) {
    if (!vk || !vk->use_command_buffer_cache) {
        return VK_NULL_HANDLE;
    }
    
    if (type == COMMAND_BUFFER_TYPE_PRIMARY) {
        if (index >= vk->image_count) {
            return VK_NULL_HANDLE;
        }
        return vk->primary_cache[index].command_buffer;
    } else if (type == COMMAND_BUFFER_TYPE_SECONDARY) {
        if (index >= vk->secondary_buffer_count) {
            return VK_NULL_HANDLE;
        }
        return vk->secondary_cache[index].command_buffer;
    }
    
    return VK_NULL_HANDLE;
}

// 标记命令缓冲区为脏
void vulkan_mark_command_buffer_dirty(struct vulkan_state* vk, command_buffer_type_t type, uint32_t index) {
    if (!vk || !vk->use_command_buffer_cache) {
        return;
    }
    
    if (type == COMMAND_BUFFER_TYPE_PRIMARY) {
        if (index < vk->image_count) {
            vk->primary_cache[index].is_dirty = true;
        }
    } else if (type == COMMAND_BUFFER_TYPE_SECONDARY) {
        if (index < vk->secondary_buffer_count) {
            vk->secondary_cache[index].is_dirty = true;
        }
    }
}

// 记录次要命令缓冲区
int vulkan_record_secondary_command_buffer(struct vulkan_state* vk, uint32_t index, 
                                           void (*record_func)(VkCommandBuffer, void*), void* user_data) {
    if (!vk || !vk->use_command_buffer_cache || !record_func || index >= vk->secondary_buffer_count) {
        return -1;
    }
    
    struct command_buffer_cache* cache = &vk->secondary_cache[index];
    
    // 检查是否需要重新记录
    if (!cache->is_dirty && cache->is_recorded) {
        return 0; // 已记录且未脏，无需重新记录
    }
    
    VkCommandBuffer cmd_buffer = cache->command_buffer;
    
    // 开始记录命令缓冲区
    VkCommandBufferInheritanceInfo inheritance_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .pNext = NULL,
        .renderPass = vk->render_pass,
        .subpass = 0,
        .framebuffer = VK_NULL_HANDLE, // 次要命令缓冲区可以在任何帧缓冲区中使用
        .occlusionQueryEnable = VK_FALSE,
        .queryFlags = 0,
        .pipelineStatistics = 0
    };
    
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        .pInheritanceInfo = &inheritance_info
    };
    
    VkResult result = vkBeginCommandBuffer(cmd_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        LOGE("Failed to begin secondary command buffer %u: %d", index, result);
        return -1;
    }
    
    // 调用用户提供的记录函数
    record_func(cmd_buffer, user_data);
    
    // 结束记录
    result = vkEndCommandBuffer(cmd_buffer);
    if (result != VK_SUCCESS) {
        LOGE("Failed to end secondary command buffer %u: %d", index, result);
        return -1;
    }
    
    // 更新缓存状态
    cache->is_recorded = true;
    cache->is_dirty = false;
    cache->last_used_frame = vk->current_frame;
    
    return 0;
}

// 执行缓存的命令缓冲区
void vulkan_execute_cached_command_buffers(struct vulkan_state* vk, VkCommandBuffer primary_buffer) {
    if (!vk || !vk->use_command_buffer_cache || primary_buffer == VK_NULL_HANDLE) {
        return;
    }
    
    // 执行所有已记录的次要命令缓冲区
    for (uint32_t i = 0; i < vk->secondary_buffer_count; i++) {
        struct command_buffer_cache* cache = &vk->secondary_cache[i];
        
        if (cache->is_recorded && !cache->is_dirty) {
            vkCmdExecuteCommands(primary_buffer, 1, &cache->command_buffer);
            cache->last_used_frame = vk->current_frame;
        }
    }
}

// 更新帧计数
void vulkan_update_frame_counter(struct vulkan_state* vk) {
    if (!vk) {
        return;
    }
    
    vk->current_frame++;
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

// 创建缓冲区
static int create_buffer(struct vulkan_state* vk, VkDeviceSize size, VkBufferUsageFlags usage, 
                         VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* buffer_memory) {
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    
    VkResult result = vkCreateBuffer(vk->device, &buffer_info, NULL, buffer);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create buffer: %d", result);
        return -1;
    }
    
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(vk->device, *buffer, &mem_requirements);
    
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = mem_requirements.size,
        .memoryTypeIndex = 0  // 需要实现查找合适内存类型的函数
    };
    
    // 简化实现：假设第一个内存类型合适
    result = vkAllocateMemory(vk->device, &alloc_info, NULL, buffer_memory);
    if (result != VK_SUCCESS) {
        LOGE("Failed to allocate buffer memory: %d", result);
        vkDestroyBuffer(vk->device, *buffer, NULL);
        return -1;
    }
    
    vkBindBufferMemory(vk->device, *buffer, *buffer_memory, 0);
    
    return 0;
}

// 初始化批处理管理器
int vulkan_init_batch_manager(struct vulkan_state* vk, uint32_t max_batches, 
                              uint32_t vertex_capacity, uint32_t index_capacity) {
    if (!vk || !vk->initialized || max_batches == 0 || vertex_capacity == 0 || index_capacity == 0) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    struct batch_manager* manager = &vk->batch_manager;
    memset(manager, 0, sizeof(struct batch_manager));
    
    // 分配批次数组
    manager->batches = (struct render_batch*)compositor_memory_alloc(sizeof(struct render_batch) * max_batches);
    if (!manager->batches) {
        LOGE("Failed to allocate batches");
        return -1;
    }
    
    // 初始化批次数组
    memset(manager->batches, 0, sizeof(struct render_batch) * max_batches);
    manager->max_batches = max_batches;
    manager->batch_count = 0;
    manager->current_batch = 0;
    
    // 创建顶点缓冲区
    if (create_buffer(vk, vertex_capacity * sizeof(float), 
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &manager->vertex_buffer, NULL) != 0) {
        LOGE("Failed to create vertex buffer");
        compositor_memory_free(manager->batches);
        return -1;
    }
    
    // 创建索引缓冲区
    if (create_buffer(vk, index_capacity * sizeof(uint32_t), 
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &manager->index_buffer, NULL) != 0) {
        LOGE("Failed to create index buffer");
        vkDestroyBuffer(vk->device, manager->vertex_buffer, NULL);
        compositor_memory_free(manager->batches);
        return -1;
    }
    
    // 映射缓冲区
    void* vertex_mapped;
    void* index_mapped;
    if (vkMapMemory(vk->device, NULL, 0, vertex_capacity * sizeof(float), 0, &vertex_mapped) != VK_SUCCESS ||
        vkMapMemory(vk->device, NULL, 0, index_capacity * sizeof(uint32_t), 0, &index_mapped) != VK_SUCCESS) {
        LOGE("Failed to map buffers");
        vkDestroyBuffer(vk->device, manager->vertex_buffer, NULL);
        vkDestroyBuffer(vk->device, manager->index_buffer, NULL);
        compositor_memory_free(manager->batches);
        return -1;
    }
    
    manager->vertex_mapped = vertex_mapped;
    manager->index_mapped = index_mapped;
    manager->vertex_capacity = vertex_capacity;
    manager->index_capacity = index_capacity;
    manager->vertex_used = 0;
    manager->index_used = 0;
    manager->initialized = true;
    
    LOGI("Batch manager initialized with %u batches, %u vertices, %u indices", 
         max_batches, vertex_capacity, index_capacity);
    
    return 0;
}

// 销毁批处理管理器
void vulkan_destroy_batch_manager(struct vulkan_state* vk) {
    if (!vk || !vk->batch_manager.initialized) {
        return;
    }
    
    struct batch_manager* manager = &vk->batch_manager;
    
    // 取消映射缓冲区
    if (manager->vertex_mapped) {
        vkUnmapMemory(vk->device, NULL);
        manager->vertex_mapped = NULL;
    }
    
    if (manager->index_mapped) {
        vkUnmapMemory(vk->device, NULL);
        manager->index_mapped = NULL;
    }
    
    // 销毁缓冲区
    if (manager->vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk->device, manager->vertex_buffer, NULL);
        manager->vertex_buffer = VK_NULL_HANDLE;
    }
    
    if (manager->index_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk->device, manager->index_buffer, NULL);
        manager->index_buffer = VK_NULL_HANDLE;
    }
    
    // 释放批次数组
    if (manager->batches) {
        compositor_memory_free(manager->batches);
        manager->batches = NULL;
    }
    
    memset(manager, 0, sizeof(struct batch_manager));
    
    LOGI("Batch manager destroyed");
}

// 开始新的批次
int vulkan_begin_batch(struct vulkan_state* vk, VkPipeline pipeline, VkDescriptorSet descriptor_set, 
                       uint32_t texture_id, bool is_transparent) {
    if (!vk || !vk->batch_manager.initialized) {
        LOGE("Batch manager not initialized");
        return -1;
    }
    
    struct batch_manager* manager = &vk->batch_manager;
    
    // 检查是否还有可用批次
    if (manager->batch_count >= manager->max_batches) {
        LOGE("Too many batches");
        return -1;
    }
    
    // 如果当前批次有数据，先结束它
    if (manager->current_batch < manager->batch_count && 
        (manager->batches[manager->current_batch].vertex_count > 0 || 
         manager->batches[manager->current_batch].index_count > 0)) {
        if (vulkan_end_batch(vk) != 0) {
            return -1;
        }
    }
    
    // 创建新批次
    manager->current_batch = manager->batch_count++;
    struct render_batch* batch = &manager->batches[manager->current_batch];
    
    batch->vertex_count = 0;
    batch->index_count = 0;
    batch->vertex_buffer = manager->vertex_buffer;
    batch->index_buffer = manager->index_buffer;
    batch->descriptor_set = descriptor_set;
    batch->pipeline = pipeline;
    batch->texture_id = texture_id;
    batch->is_transparent = is_transparent;
    
    return 0;
}

// 添加数据到当前批次
int vulkan_add_to_batch(struct vulkan_state* vk, const void* vertices, uint32_t vertex_count,
                        const uint32_t* indices, uint32_t index_count) {
    if (!vk || !vk->batch_manager.initialized || !vertices || !indices || 
        vertex_count == 0 || index_count == 0) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    struct batch_manager* manager = &vk->batch_manager;
    
    // 检查是否有足够的空间
    if (manager->vertex_used + vertex_count > manager->vertex_capacity ||
        manager->index_used + index_count > manager->index_capacity) {
        LOGE("Not enough space in buffers");
        return -1;
    }
    
    struct render_batch* batch = &manager->batches[manager->current_batch];
    
    // 复制顶点数据
    memcpy((char*)manager->vertex_mapped + manager->vertex_used * sizeof(float), 
           vertices, vertex_count * sizeof(float));
    
    // 复制索引数据，并调整索引值
    uint32_t base_vertex = manager->vertex_used;
    for (uint32_t i = 0; i < index_count; i++) {
        ((uint32_t*)manager->index_mapped)[manager->index_used + i] = base_vertex + indices[i];
    }
    
    // 更新计数
    batch->vertex_count += vertex_count;
    batch->index_count += index_count;
    manager->vertex_used += vertex_count;
    manager->index_used += index_count;
    
    return 0;
}

// 结束当前批次
int vulkan_end_batch(struct vulkan_state* vk) {
    if (!vk || !vk->batch_manager.initialized) {
        LOGE("Batch manager not initialized");
        return -1;
    }
    
    struct batch_manager* manager = &vk->batch_manager;
    
    // 检查是否有当前批次
    if (manager->current_batch >= manager->batch_count) {
        LOGE("No current batch");
        return -1;
    }
    
    struct render_batch* batch = &manager->batches[manager->current_batch];
    
    // 如果批次为空，移除它
    if (batch->vertex_count == 0 && batch->index_count == 0) {
        manager->batch_count--;
        if (manager->current_batch >= manager->batch_count) {
            manager->current_batch = manager->batch_count > 0 ? manager->batch_count - 1 : 0;
        }
    }
    
    return 0;
}

// 渲染所有批次
int vulkan_render_batches(struct vulkan_state* vk, VkCommandBuffer command_buffer) {
    if (!vk || !vk->batch_manager.initialized || command_buffer == VK_NULL_HANDLE) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    struct batch_manager* manager = &vk->batch_manager;
    
    // 绑定顶点和索引缓冲区
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &manager->vertex_buffer, offsets);
    vkCmdBindIndexBuffer(command_buffer, manager->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    
    // 渲染所有批次
    uint32_t index_offset = 0;
    for (uint32_t i = 0; i < manager->batch_count; i++) {
        struct render_batch* batch = &manager->batches[i];
        
        if (batch->index_count > 0) {
            // 绑定管线和描述符集
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                   VK_NULL_HANDLE, 0, 1, &batch->descriptor_set, 0, NULL);
            
            // 绘制
            vkCmdDrawIndexed(command_buffer, batch->index_count, 1, index_offset, 0, 0);
            
            index_offset += batch->index_count;
        }
    }
    
    return 0;
}