#include "compositor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <dlfcn.h>
#include <android/log.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <wlr/backend/headless.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xwayland.h>
#include <wlr/util/log.h>
#include <wayland-server-core.h>

// 添加SURFACE_VALID宏定义，用于简化对surface及surface->surface的有效性检查
#define SURFACE_VALID(surface) ((surface) && (surface)->surface)

// Xwayland窗口状态结构体
typedef struct {
    struct wl_list link;
    struct wlr_xwayland_surface *surface;
    WindowSavedState state;
    int z_order;
} XwaylandWindowState;

// Wayland窗口状态结构体
typedef struct {
    struct wl_list link;
    struct WaylandWindow *window;
    WindowSavedState state;
    int z_order;
} WaylandWindowState;

// Wayland窗口结构体定义
typedef struct {
    struct wl_list link;
    struct wlr_surface *surface;
    char *title;
    int x, y;
    int width, height;
    bool mapped;
    bool minimized;
    bool maximized;
    float opacity;
    int z_order;
} WaylandWindow;

// 全局窗口状态列表
static struct wl_list xwayland_window_states_list = WL_LIST_HEAD_INIT(xwayland_window_states_list);
static struct wl_list wayland_window_states_list = WL_LIST_HEAD_INIT(wayland_window_states_list);

// 获取默认配置
CompositorConfig* compositor_get_default_config(void) {
    static CompositorConfig default_config = {
        // Xwayland 配置
        .enable_xwayland = true,
        .xwayland_path = "/usr/bin/Xwayland", // 默认路径，实际路径会在运行时检测
        .xwayland_display_number = 0,
        
        // 渲染配置
        .enable_vsync = true,
        .preferred_refresh_rate = 60, // 默认60Hz
        .max_swapchain_images = 3,
        .initial_scale = 1.0f,
        
        // 窗口管理
        .default_window_width = 800,
        .default_window_height = 600,
        .enable_window_decoration = true,
        
        // 调试选项
        .log_level = 2, // 默认INFO级别
        .enable_tracing = false,
        .enable_perf_monitoring = false,
        
        // 其他配置
        .background_color = {0.1f, 0.1f, 0.1f}, // 默认暗色背景
        .enable_debug_logging = false // 默认禁用调试日志
    };
    
    return &default_config;
}

// 设置日志级别
void compositor_set_log_level(int level) {
    if (level >= 0 && level <= 3) {
        compositor_state.config.log_level = level;
        log_message(2, "Log level set to %d", level);
    }
}

// 全局状态
static struct {
    // 配置参数
    CompositorConfig config;
    
    // 错误信息
    int last_error;
    char error_message[512];
    
    // Wayland 服务器
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_compositor *compositor;
    struct wlr_subcompositor *subcompositor;
    struct wlr_output_layout *output_layout;
    struct wlr_output *output;
    struct wlr_xwayland *xwayland;
    
    // Xwayland 进程管理
    pid_t xwayland_pid;
    char socket_path[256];
    char display_str[64];
    
    // Vulkan 相关
    void *vulkan_lib; // Vulkan库句柄
    VkInstance vulkan_instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    VkImage *swapchain_images;
    VkImageView *swapchain_image_views;
    uint32_t swapchain_image_count;
    uint32_t graphics_queue_family;
    
    // 同步对象
    VkSemaphore image_available_semaphore;
    VkSemaphore render_finished_semaphore;
    VkFence in_flight_fence;
    
    // Android 相关
    ANativeWindow *window;
    int width, height;
    
    // 单线程同步
    int epoll_fd;
    struct epoll_event display_event;
    
    // 窗口管理
    struct wl_list surfaces;           // Wayland窗口列表
    struct wl_list xwayland_surfaces;  // Xwayland窗口列表
    struct wlr_xwayland_surface *active_surface;
    
    // 性能统计
    int frame_count;
    double last_fps_time;
    float current_fps;
    
    // 脏区域管理
    bool use_dirty_rect;
    struct wlr_box dirty_rect;
    bool full_redraw_needed;
    
    // 初始化状态
    bool initialized;
    bool running;
} compositor_state = {
    .last_error = COMPOSITOR_SUCCESS,
    .epoll_fd = -1,
    .surfaces = WL_LIST_HEAD_INIT(compositor_state.surfaces),
    .xwayland_surfaces = WL_LIST_HEAD_INIT(compositor_state.xwayland_surfaces),
    .use_dirty_rect = true,
    .dirty_rect = {0, 0, 0, 0},
    .full_redraw_needed = true
};

// 事件监听器
static struct wl_listener xwayland_ready_listener = {0};
static struct wl_listener xwayland_new_surface_listener = {0};
static struct wl_listener new_surface_listener = {0};

// 日志级别映射
#define LOG_TAG "WinDroidsCompositor"

static void log_message(int level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (level <= compositor_state.config.log_level) {
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        
        // 输出到Android日志系统
        switch (level) {
            case 0: // ERROR
                __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", buffer);
                break;
            case 1: // WARN
                __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "%s", buffer);
                break;
            case 2: // INFO
                __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buffer);
                break;
            case 3: // DEBUG
                __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", buffer);
                break;
        }
        
        // 同时输出到标准错误
        fprintf(stderr, "%s: %s\n", LOG_TAG, buffer);
    }
    
    va_end(args);
}

// 标记脏区域
static void mark_dirty_rect(int x, int y, int width, int height) {
    if (!compositor_state.initialized) {
        return;
    }
    
    if (!compositor_state.use_dirty_rect) {
        compositor_state.full_redraw_needed = true;
        return;
    }
    
    // 限制在屏幕范围内
    x = MAX(x, 0);
    y = MAX(y, 0);
    width = MIN(width, compositor_state.width - x);
    height = MIN(height, compositor_state.height - y);
    
    if (width <= 0 || height <= 0) {
        return;
    }
    
    // 验证参数
    if (x < 0 || y < 0 || 
        x >= compositor_state.width || 
        y >= compositor_state.height) {
        log_message(1, "Invalid dirty rect coordinates: %d,%d,%d,%d", x, y, width, height);
        return;
    }
    
    // 计算脏区域边界
    int right = MIN(x + width, compositor_state.width);
    int bottom = MIN(y + height, compositor_state.height);
    x = MAX(x, 0);
    y = MAX(y, 0);
    width = right - x;
    height = bottom - y;
    
    if (width <= 0 || height <= 0) {
        return;
    }
    
    // 如果当前没有脏区域，则直接设置
    if (compositor_state.dirty_rect.width == 0 || compositor_state.dirty_rect.height == 0) {
        compositor_state.dirty_rect.x = x;
        compositor_state.dirty_rect.y = y;
        compositor_state.dirty_rect.width = width;
        compositor_state.dirty_rect.height = height;
    } else {
        // 合并到现有脏区域
        int new_x = MIN(compositor_state.dirty_rect.x, x);
        int new_y = MIN(compositor_state.dirty_rect.y, y);
        int new_right = MAX(compositor_state.dirty_rect.x + compositor_state.dirty_rect.width, x + width);
        int new_bottom = MAX(compositor_state.dirty_rect.y + compositor_state.dirty_rect.height, y + height);
        
        compositor_state.dirty_rect.x = new_x;
        compositor_state.dirty_rect.y = new_y;
        compositor_state.dirty_rect.width = new_right - new_x;
        compositor_state.dirty_rect.height = new_bottom - new_y;
    }
    
    // 检查是否需要全屏重绘（如果脏区域超过屏幕的80%）
    int screen_area = compositor_state.width * compositor_state.height;
    int dirty_area = compositor_state.dirty_rect.width * compositor_state.dirty_rect.height;
    
    if (dirty_area > screen_area * 0.8) {
        log_message(2, "Dirty area exceeds 80%% of screen, triggering full redraw");
        compositor_state.dirty_rect.x = 0;
        compositor_state.dirty_rect.y = 0;
        compositor_state.dirty_rect.width = compositor_state.width;
        compositor_state.dirty_rect.height = compositor_state.height;
    }
    
    log_message(3, "Dirty rect updated to: %d,%d,%d,%d (area: %d/%d)", 
                compositor_state.dirty_rect.x, compositor_state.dirty_rect.y,
                compositor_state.dirty_rect.width, compositor_state.dirty_rect.height,
                dirty_area, screen_area);
}

// 标记全屏重绘
static void mark_full_redraw(void) {
    compositor_state.full_redraw_needed = true;
    compositor_state.dirty_rect.x = 0;
    compositor_state.dirty_rect.y = 0;
    compositor_state.dirty_rect.width = compositor_state.width;
    compositor_state.dirty_rect.height = compositor_state.height;
    log_message(3, "Marked full redraw");
}

// 获取错误码描述
static const char* get_error_description(int error_code) {
    switch (error_code) {
        case COMPOSITOR_SUCCESS: return "Success";
        case COMPOSITOR_ERROR_INIT: return "Initialization error";
        case COMPOSITOR_ERROR_VULKAN: return "Vulkan API error";
        case COMPOSITOR_ERROR_XWAYLAND: return "Xwayland error";
        case COMPOSITOR_ERROR_WLROOTS: return "wlroots API error";
        case COMPOSITOR_ERROR_MEMORY: return "Memory allocation error";
        case COMPOSITOR_ERROR_INVALID_ARGS: return "Invalid arguments";
        case COMPOSITOR_ERROR_NOT_INITIALIZED: return "Not initialized";
        case COMPOSITOR_ERROR_SURFACE_ERROR: return "Surface error";
        case COMPOSITOR_ERROR_INPUT_DEVICE_ERROR: return "Input device error";
        case COMPOSITOR_ERROR_SWAPCHAIN_ERROR: return "Swapchain error";
        case COMPOSITOR_ERROR_CONFIG_ERROR: return "Configuration error";
        case COMPOSITOR_ERROR_WINDOW_NOT_FOUND: return "Window not found";
        case COMPOSITOR_ERROR_UNSUPPORTED_OPERATION: return "Unsupported operation";
        case COMPOSITOR_ERROR_SYSTEM: return "System error";
        case COMPOSITOR_ERROR_INVALID_STATE: return "Invalid state";
        case COMPOSITOR_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case COMPOSITOR_ERROR_RESOURCE_EXHAUSTED: return "Resource exhausted";
        case COMPOSITOR_ERROR_TIMEOUT: return "Operation timed out";
        case COMPOSITOR_ERROR_UNEXPECTED: return "Unexpected error";
        default: return "Unknown error";
    }
}

// 设置错误信息
static void set_error(int error_code, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    compositor_state.last_error = error_code;
    
    // 构建统一格式的错误消息
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    
    snprintf(compositor_state.error_message, sizeof(compositor_state.error_message), 
             "[%s] %s", get_error_description(error_code), buffer);
    
    log_message(0, "[ERROR] Code: %d, %s", error_code, compositor_state.error_message);
    
    va_end(args);
}

// 清除错误信息
static void clear_error(void) {
    compositor_state.last_error = COMPOSITOR_SUCCESS;
    compositor_state.error_message[0] = '\0';
}



// 获取Xwayland窗口状态
static XwaylandWindowState* get_xwayland_window_state(struct wlr_xwayland_surface *surface) {
    XwaylandWindowState *state;
    wl_list_for_each(state, &xwayland_window_states_list, link) {
        if (state->surface == surface) {
            return state;
        }
    }
    
    // 如果找不到，创建新的状态结构体
    state = calloc(1, sizeof(XwaylandWindowState));
    if (!state) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for Xwayland window state");
        return NULL;
    }
    
    state->surface = surface;
    state->state.state = WINDOW_STATE_NORMAL;
    state->state.is_fullscreen = false;
    wl_list_insert(&xwayland_window_states_list, &state->link);
    
    return state;
}

// 获取Wayland窗口状态
static WaylandWindowState* get_wayland_window_state(struct WaylandWindow *window) {
    WaylandWindowState *state;
    wl_list_for_each(state, &wayland_window_states_list, link) {
        if (state->window == window) {
            return state;
        }
    }
    
    // 如果找不到，创建新的状态结构体
    state = calloc(1, sizeof(WaylandWindowState));
    if (!state) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for Wayland window state");
        return NULL;
    }
    
    state->window = window;
    state->state.state = WINDOW_STATE_NORMAL;
    state->state.is_fullscreen = false;
    wl_list_insert(&wayland_window_states_list, &state->link);
    
    return state;
}

// 移除Xwayland窗口状态
static void remove_xwayland_window_state(struct wlr_xwayland_surface *surface) {
    XwaylandWindowState *state, *tmp;
    wl_list_for_each_safe(state, tmp, &xwayland_window_states_list, link) {
        if (state->surface == surface) {
            wl_list_remove(&state->link);
            free(state);
            return;
        }
    }
}

// 移除Wayland窗口状态
static void remove_wayland_window_state(struct WaylandWindow *window) {
    WaylandWindowState *state, *tmp;
    wl_list_for_each_safe(state, tmp, &wayland_window_states_list, link) {
        if (state->window == window) {
            wl_list_remove(&state->link);
            free(state);
            return;
        }
    }
}

// Vulkan 函数指针（动态加载）
#define DEFINE_VK_FUNC(name) PFN_##name name = NULL
DEFINE_VK_FUNC(vkCreateInstance);
DEFINE_VK_FUNC(vkDestroyInstance);
DEFINE_VK_FUNC(vkEnumeratePhysicalDevices);
DEFINE_VK_FUNC(vkGetPhysicalDeviceProperties);
DEFINE_VK_FUNC(vkGetPhysicalDeviceQueueFamilyProperties);
DEFINE_VK_FUNC(vkCreateDevice);
DEFINE_VK_FUNC(vkGetDeviceQueue);
DEFINE_VK_FUNC(vkCreateAndroidSurfaceKHR);
DEFINE_VK_FUNC(vkDestroySurfaceKHR);
DEFINE_VK_FUNC(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
DEFINE_VK_FUNC(vkGetPhysicalDeviceSurfaceFormatsKHR);
DEFINE_VK_FUNC(vkGetPhysicalDeviceSurfacePresentModesKHR);
DEFINE_VK_FUNC(vkCreateSwapchainKHR);
DEFINE_VK_FUNC(vkDestroySwapchainKHR);
DEFINE_VK_FUNC(vkGetSwapchainImagesKHR);
DEFINE_VK_FUNC(vkCreateImageView);
DEFINE_VK_FUNC(vkDestroyImageView);
DEFINE_VK_FUNC(vkAcquireNextImageKHR);
DEFINE_VK_FUNC(vkQueuePresentKHR);
DEFINE_VK_FUNC(vkCreateSemaphore);
DEFINE_VK_FUNC(vkDestroySemaphore);
DEFINE_VK_FUNC(vkCreateFence);
DEFINE_VK_FUNC(vkDestroyFence);
DEFINE_VK_FUNC(vkWaitForFences);
DEFINE_VK_FUNC(vkResetFences);
DEFINE_VK_FUNC(vkBeginCommandBuffer);
DEFINE_VK_FUNC(vkEndCommandBuffer);
DEFINE_VK_FUNC(vkQueueSubmit);
#undef DEFINE_VK_FUNC

// 动态加载 Vulkan 函数
static bool load_vulkan_functions() {
    void *libvulkan = dlopen("libvulkan.so", RTLD_NOW);
    if (!libvulkan) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to load libvulkan.so: %s", dlerror());
        return false;
    }
    
    // 保存库指针以便后续清理
    compositor_state.vulkan_lib = libvulkan;
    
#define LOAD_VK_FUNC(name) \
    name = (PFN_##name)dlsym(libvulkan, #name); \
    if (!name) { \
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to load %s", #name); \
        dlclose(libvulkan); \
        return false; \
    }
    
    LOAD_VK_FUNC(vkCreateInstance);
    LOAD_VK_FUNC(vkDestroyInstance);
    LOAD_VK_FUNC(vkEnumeratePhysicalDevices);
    LOAD_VK_FUNC(vkGetPhysicalDeviceProperties);
    LOAD_VK_FUNC(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_VK_FUNC(vkCreateDevice);
    LOAD_VK_FUNC(vkGetDeviceQueue);
    LOAD_VK_FUNC(vkCreateAndroidSurfaceKHR);
    LOAD_VK_FUNC(vkDestroySurfaceKHR);
    LOAD_VK_FUNC(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD_VK_FUNC(vkGetPhysicalDeviceSurfaceFormatsKHR);
    LOAD_VK_FUNC(vkGetPhysicalDeviceSurfacePresentModesKHR);
    LOAD_VK_FUNC(vkCreateSwapchainKHR);
    LOAD_VK_FUNC(vkDestroySwapchainKHR);
    LOAD_VK_FUNC(vkGetSwapchainImagesKHR);
    LOAD_VK_FUNC(vkCreateImageView);
    LOAD_VK_FUNC(vkDestroyImageView);
    LOAD_VK_FUNC(vkAcquireNextImageKHR);
    LOAD_VK_FUNC(vkQueuePresentKHR);
    LOAD_VK_FUNC(vkCreateSemaphore);
    LOAD_VK_FUNC(vkDestroySemaphore);
    LOAD_VK_FUNC(vkCreateFence);
    LOAD_VK_FUNC(vkDestroyFence);
    LOAD_VK_FUNC(vkWaitForFences);
    LOAD_VK_FUNC(vkResetFences);
    LOAD_VK_FUNC(vkBeginCommandBuffer);
    LOAD_VK_FUNC(vkEndCommandBuffer);
    LOAD_VK_FUNC(vkQueueSubmit);
    
#undef LOAD_VK_FUNC
    
    log_message(2, "Vulkan functions loaded successfully");
    return true;
}

// 初始化 Vulkan
static bool init_vulkan() {
    clear_error(); // 清除之前的错误状态
    
    if (!load_vulkan_functions()) {
        return false;
    }
    
    // 创建 Vulkan 实例
    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
    
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "WinDroids Compositor",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Custom Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_2
    };
    
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]),
        .ppEnabledExtensionNames = extensions
    };
    
    if (vkCreateInstance(&create_info, NULL, &compositor_state.vulkan_instance) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to create Vulkan instance");
        return false;
    }
    
    // 选择物理设备
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(compositor_state.vulkan_instance, &device_count, NULL);
    if (device_count == 0) {
        set_error(COMPOSITOR_ERROR_VULKAN, "No Vulkan physical devices found");
        cleanup_vulkan();
        return false;
    }
    
    VkPhysicalDevice *devices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * device_count);
    if (!devices) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for physical devices");
        cleanup_vulkan();
        return false;
    }
    
    if (vkEnumeratePhysicalDevices(compositor_state.vulkan_instance, &device_count, devices) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to enumerate physical devices");
        free(devices);
        cleanup_vulkan();
        return false;
    }
    
    // 选择合适的物理设备（优先选择有GPU的设备）
    compositor_state.physical_device = devices[0];
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(compositor_state.physical_device, &properties);
    log_message(2, "Selected physical device: %s", properties.deviceName);
    free(devices);
    
    // 找到图形队列族
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(compositor_state.physical_device, &queue_family_count, NULL);
    if (queue_family_count == 0) {
        set_error(COMPOSITOR_ERROR_VULKAN, "No queue families found");
        cleanup_vulkan();
        return false;
    }
    
    VkQueueFamilyProperties *queue_families = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
    if (!queue_families) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for queue families");
        cleanup_vulkan();
        return false;
    }
    
    vkGetPhysicalDeviceQueueFamilyProperties(compositor_state.physical_device, &queue_family_count, queue_families);
    
    compositor_state.graphics_queue_family = 0;
    for (; compositor_state.graphics_queue_family < queue_family_count; compositor_state.graphics_queue_family++) {
        if (queue_families[compositor_state.graphics_queue_family].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            break;
        }
    }
    
    if (compositor_state.graphics_queue_family >= queue_family_count) {
        set_error(COMPOSITOR_ERROR_VULKAN, "No graphics queue family found");
        free(queue_families);
        cleanup_vulkan();
        return false;
    }
    
    free(queue_families);
    
    // 创建逻辑设备
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = compositor_state.graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };
    
    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = sizeof(device_extensions) / sizeof(device_extensions[0]),
        .ppEnabledExtensionNames = device_extensions
    };
    
    if (vkCreateDevice(compositor_state.physical_device, &device_create_info, NULL, &compositor_state.device) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to create Vulkan device");
        cleanup_vulkan();
        return false;
    }
    
    vkGetDeviceQueue(compositor_state.device, compositor_state.graphics_queue_family, 0, &compositor_state.queue);
    
    // 创建 Android 表面
    VkAndroidSurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = compositor_state.window
    };
    
    if (vkCreateAndroidSurfaceKHR(compositor_state.vulkan_instance, &surface_create_info, NULL, &compositor_state.surface) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to create Android surface");
        cleanup_vulkan();
        return false;
    }
    
    // 获取表面能力
    VkSurfaceCapabilitiesKHR capabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(compositor_state.physical_device, compositor_state.surface, &capabilities) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to get surface capabilities");
        cleanup_vulkan();
        return false;
    }
    
    // 设置交换链扩展
    compositor_state.swapchain_extent.width = compositor_state.width;
    compositor_state.swapchain_extent.height = compositor_state.height;
    if (compositor_state.swapchain_extent.width < capabilities.minImageExtent.width) {
        compositor_state.swapchain_extent.width = capabilities.minImageExtent.width;
    }
    if (compositor_state.swapchain_extent.width > capabilities.maxImageExtent.width) {
        compositor_state.swapchain_extent.width = capabilities.maxImageExtent.width;
    }
    if (compositor_state.swapchain_extent.height < capabilities.minImageExtent.height) {
        compositor_state.swapchain_extent.height = capabilities.minImageExtent.height;
    }
    if (compositor_state.swapchain_extent.height > capabilities.maxImageExtent.height) {
        compositor_state.swapchain_extent.height = capabilities.maxImageExtent.height;
    }
    
    log_message(2, "Swapchain extent: %ux%u", 
                compositor_state.swapchain_extent.width, 
                compositor_state.swapchain_extent.height);
    
    // 获取表面格式
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(compositor_state.physical_device, compositor_state.surface, &format_count, NULL);
    if (format_count == 0) {
        set_error(COMPOSITOR_ERROR_VULKAN, "No surface formats available");
        cleanup_vulkan();
        return false;
    }
    
    VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR*)malloc(sizeof(VkSurfaceFormatKHR) * format_count);
    if (!formats) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for surface formats");
        cleanup_vulkan();
        return false;
    }
    
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(compositor_state.physical_device, compositor_state.surface, &format_count, formats) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to get surface formats");
        free(formats);
        cleanup_vulkan();
        return false;
    }
    
    // 优先选择RGBA8格式
    compositor_state.swapchain_format = formats[0].format;
    VkColorSpaceKHR color_space = formats[0].colorSpace;
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            compositor_state.swapchain_format = formats[i].format;
            color_space = formats[i].colorSpace;
            break;
        }
    }
    
    free(formats);
    
    // 获取呈现模式
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(compositor_state.physical_device, compositor_state.surface, &present_mode_count, NULL);
    if (present_mode_count == 0) {
        set_error(COMPOSITOR_ERROR_VULKAN, "No present modes available");
        cleanup_vulkan();
        return false;
    }
    
    VkPresentModeKHR *present_modes = (VkPresentModeKHR*)malloc(sizeof(VkPresentModeKHR) * present_mode_count);
    if (!present_modes) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for present modes");
        cleanup_vulkan();
        return false;
    }
    
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(compositor_state.physical_device, compositor_state.surface, &present_mode_count, present_modes) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to get present modes");
        free(present_modes);
        cleanup_vulkan();
        return false;
    }
    
    // 优先选择MAILBOX模式，其次是FIFO_RELAXED，最后是FIFO
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < present_mode_count; i++) {
        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        } else if (present_modes[i] == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
            present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        }
    }
    
    free(present_modes);
    log_message(2, "Selected present mode: %u", present_mode);
    
    // 确定交换链图像数量
    uint32_t image_count = compositor_state.config.max_swapchain_images;
    if (image_count < capabilities.minImageCount) {
        image_count = capabilities.minImageCount;
    }
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }
    
    // 创建交换链
    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = compositor_state.surface,
        .minImageCount = image_count,
        .imageFormat = compositor_state.swapchain_format,
        .imageColorSpace = color_space,
        .imageExtent = compositor_state.swapchain_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };
    
    if (vkCreateSwapchainKHR(compositor_state.device, &swapchain_create_info, NULL, &compositor_state.swapchain) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to create swapchain");
        cleanup_vulkan();
        return false;
    }
    
    // 获取交换链图像
    vkGetSwapchainImagesKHR(compositor_state.device, compositor_state.swapchain, &compositor_state.swapchain_image_count, NULL);
    
    compositor_state.swapchain_images = (VkImage*)malloc(sizeof(VkImage) * compositor_state.swapchain_image_count);
    if (!compositor_state.swapchain_images) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for swapchain images");
        cleanup_vulkan();
        return false;
    }
    
    if (vkGetSwapchainImagesKHR(compositor_state.device, compositor_state.swapchain, &compositor_state.swapchain_image_count, compositor_state.swapchain_images) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to get swapchain images");
        cleanup_vulkan();
        return false;
    }
    
    // 创建图像视图
    compositor_state.swapchain_image_views = (VkImageView*)malloc(sizeof(VkImageView) * compositor_state.swapchain_image_count);
    if (!compositor_state.swapchain_image_views) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for swapchain image views");
        cleanup_vulkan();
        return false;
    }
    
    for (uint32_t i = 0; i < compositor_state.swapchain_image_count; i++) {
        VkImageViewCreateInfo view_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = compositor_state.swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = compositor_state.swapchain_format,
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
        
        if (vkCreateImageView(compositor_state.device, &view_create_info, NULL, &compositor_state.swapchain_image_views[i]) != VK_SUCCESS) {
            set_error(COMPOSITOR_ERROR_VULKAN, "Failed to create image view %u", i);
            cleanup_vulkan();
            return false;
        }
    }
    
    // 创建同步对象
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    
    if (vkCreateSemaphore(compositor_state.device, &semaphore_info, NULL, &compositor_state.image_available_semaphore) != VK_SUCCESS ||
        vkCreateSemaphore(compositor_state.device, &semaphore_info, NULL, &compositor_state.render_finished_semaphore) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to create semaphores");
        cleanup_vulkan();
        return false;
    }
    
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    
    if (vkCreateFence(compositor_state.device, &fence_info, NULL, &compositor_state.in_flight_fence) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to create fence");
        cleanup_vulkan();
        return false;
    }
    
    log_message(1, "Vulkan initialized successfully with %u swapchain images", compositor_state.swapchain_image_count);
    return true;
}

// 清理 Vulkan 资源
static void cleanup_vulkan() {
    // 按正确顺序释放资源
    if (compositor_state.device != VK_NULL_HANDLE) {
        // 先等待设备空闲
        if (vkDeviceWaitIdle) {
            vkDeviceWaitIdle(compositor_state.device);
        }
        
        // 销毁同步对象
        if (compositor_state.in_flight_fence != VK_NULL_HANDLE) {
            vkDestroyFence(compositor_state.device, compositor_state.in_flight_fence, NULL);
            compositor_state.in_flight_fence = VK_NULL_HANDLE;
        }
        
        if (compositor_state.render_finished_semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(compositor_state.device, compositor_state.render_finished_semaphore, NULL);
            compositor_state.render_finished_semaphore = VK_NULL_HANDLE;
        }
        
        if (compositor_state.image_available_semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(compositor_state.device, compositor_state.image_available_semaphore, NULL);
            compositor_state.image_available_semaphore = VK_NULL_HANDLE;
        }
        
        // 销毁图像视图
        if (compositor_state.swapchain_image_views) {
            for (uint32_t i = 0; i < compositor_state.swapchain_image_count; i++) {
                if (compositor_state.swapchain_image_views[i] != VK_NULL_HANDLE) {
                    vkDestroyImageView(compositor_state.device, compositor_state.swapchain_image_views[i], NULL);
                }
            }
            free(compositor_state.swapchain_image_views);
            compositor_state.swapchain_image_views = NULL;
        }
        
        // 释放图像数组
        if (compositor_state.swapchain_images) {
            free(compositor_state.swapchain_images);
            compositor_state.swapchain_images = NULL;
        }
        
        // 销毁交换链
        if (compositor_state.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(compositor_state.device, compositor_state.swapchain, NULL);
            compositor_state.swapchain = VK_NULL_HANDLE;
        }
        
        // 销毁设备
        vkDestroyDevice(compositor_state.device, NULL);
        compositor_state.device = VK_NULL_HANDLE;
    }
    
    // 销毁表面
    if (compositor_state.surface != VK_NULL_HANDLE && compositor_state.vulkan_instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(compositor_state.vulkan_instance, compositor_state.surface, NULL);
        compositor_state.surface = VK_NULL_HANDLE;
    }
    
    // 销毁实例
    if (compositor_state.vulkan_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(compositor_state.vulkan_instance, NULL);
        compositor_state.vulkan_instance = VK_NULL_HANDLE;
    }
    
    log_message(2, "Vulkan resources cleaned up");
}

// 启动 Xwayland 子进程
static bool start_xwayland() {
    // 创建 Wayland socket
    snprintf(compositor_state.socket_path, sizeof(compositor_state.socket_path), "/data/local/tmp/windroids-%d.sock", getpid());
    snprintf(compositor_state.display_str, sizeof(compositor_state.display_str), ":0");
    
    // 移除旧的 socket
    unlink(compositor_state.socket_path);
    
    // 设置环境变量
    setenv("WAYLAND_DISPLAY", compositor_state.socket_path, 1);
    setenv("DISPLAY", compositor_state.display_str, 1);
    
    // 启动 Xwayland
    compositor_state.xwayland_pid = fork();
    if (compositor_state.xwayland_pid == -1) {
        perror("fork");
        return false;
    }
    
    if (compositor_state.xwayland_pid == 0) {
        // 子进程
        char* const args[] = {
            "/data/app/com.example.windroids/lib/arm64/xwayland",
            compositor_state.display_str,
            "-rootless",
            "-terminate",
            "-listen-tcp",
            "-noreset",
            NULL
        };
        
        execv(args[0], args);
        perror("execv");
        exit(1);
    }
    
    // 等待 Xwayland 启动
    usleep(100000);
    
    return true;
}

// 停止 Xwayland 子进程
static void stop_xwayland() {
    if (compositor_state.xwayland_pid > 0) {
        kill(compositor_state.xwayland_pid, SIGTERM);
        waitpid(compositor_state.xwayland_pid, NULL, 0);
        compositor_state.xwayland_pid = 0;
    }
    
    if (compositor_state.socket_path[0]) {
        unlink(compositor_state.socket_path);
        compositor_state.socket_path[0] = '\0';
    }
}

// wlroots 事件处理
// 定义Wayland表面包装结构
typedef struct {
    struct wlr_surface *surface;
    struct wl_list link;
    int x, y;
    int width, height;
    char* title;
    bool mapped;
    struct wl_listener destroy_listener;
    struct wl_listener map_listener;
    struct wl_listener unmap_listener;
    struct wl_listener commit_listener;
} WaylandWindow;

static void handle_wayland_surface_destroy(struct wl_listener* listener, void* data) {
    WaylandWindow* window = wl_container_of(listener, window, destroy_listener);
    log_message(2, "Wayland surface destroyed: %s", window->title ? window->title : "(unnamed)");
    
    // 从列表中移除
    if (!wl_list_empty(&window->link)) {
        wl_list_remove(&window->link);
    }
    
    // 如果销毁的是活动表面，选择下一个表面作为活动表面
    if (compositor_state.active_surface == (struct wlr_xwayland_surface*)window) {
        if (!wl_list_empty(&compositor_state.surfaces)) {
            compositor_state.active_surface = wl_container_of(
                compositor_state.surfaces.next, compositor_state.active_surface, link);
            log_message(3, "Active surface changed to: %s", 
                        compositor_state.active_surface->title ? 
                        compositor_state.active_surface->title : "(unnamed)");
        } else {
            compositor_state.active_surface = NULL;
            log_message(3, "No active surfaces remaining");
        }
    }
    
    // 释放资源
    if (window->title) {
        free(window->title);
    }
    free(window);
}

static void handle_wayland_surface_map(struct wl_listener* listener, void* data) {
    WaylandWindow* window = wl_container_of(listener, window, map_listener);
    window->mapped = true;
    log_message(3, "Wayland surface mapped");
}

static void handle_wayland_surface_unmap(struct wl_listener* listener, void* data) {
    WaylandWindow* window = wl_container_of(listener, window, unmap_listener);
    window->mapped = false;
    log_message(3, "Wayland surface unmapped");
}

static void handle_wayland_surface_commit(struct wl_listener* listener, void* data) {
    WaylandWindow* window = wl_container_of(listener, window, commit_listener);
    // 更新窗口大小
    if (window->surface && window->surface->current) {
        window->width = window->surface->current->width;
        window->height = window->surface->current->height;
    }
}

static void handle_new_surface(struct wl_listener *listener, void *data) {
    // 处理新的 Wayland 表面
    struct wlr_surface* surface = data;
    log_message(2, "New Wayland surface created");
    
    // 创建窗口管理结构
    WaylandWindow* window = calloc(1, sizeof(WaylandWindow));
    if (!window) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate Wayland window");
        return;
    }
    
    // 初始化窗口属性
    window->surface = surface;
    window->x = 100; // 默认位置
    window->y = 100; // 默认位置
    window->width = 800; // 默认宽度
    window->height = 600; // 默认高度
    window->mapped = false;
    window->title = strdup("Wayland Window"); // 默认标题
    
    // 初始化窗口状态
    WaylandWindowState *window_state = get_wayland_window_state(window);
    if (window_state) {
        // 保存初始窗口位置和大小
        window_state->state.saved_x = window->x;
        window_state->state.saved_y = window->y;
        window_state->state.saved_width = window->width;
        window_state->state.saved_height = window->height;
        window_state->state.state = WINDOW_STATE_NORMAL;
    }
    
    // 设置监听器
    window->destroy_listener.notify = handle_wayland_surface_destroy;
    wl_signal_add(&surface->events.destroy, &window->destroy_listener);
    
    window->map_listener.notify = handle_wayland_surface_map;
    wl_signal_add(&surface->events.map, &window->map_listener);
    
    window->unmap_listener.notify = handle_wayland_surface_unmap;
    wl_signal_add(&surface->events.unmap, &window->unmap_listener);
    
    window->commit_listener.notify = handle_wayland_surface_commit;
    wl_signal_add(&surface->events.commit, &window->commit_listener);
    
    // 将窗口添加到管理列表
    wl_list_insert(&compositor_state.surfaces, &window->link);
    
    // 如果没有活动窗口，设置为活动窗口
    if (!compositor_state.active_surface) {
        compositor_state.active_surface = (struct wlr_xwayland_surface*)window;
        log_message(3, "Set active surface to new Wayland window");
    }
}

static void handle_xwayland_ready(struct wl_listener *listener, void *data) {
    // Xwayland 已准备就绪
    fprintf(stderr, "Xwayland ready\n");
}

// 处理Wayland表面销毁事件
static void handle_wayland_surface_destroy(struct wl_listener* listener, void* data) {
    // 从监听器数据中获取窗口指针
    struct WaylandWindow *window = listener->data;
    
    log_message(2, "Wayland window destroyed");
    
    // 从列表中移除
    if (!wl_list_empty(&window->link)) {
        wl_list_remove(&window->link);
    }
    
    // 如果销毁的是活动窗口，选择下一个窗口作为活动窗口
    if (compositor_state.active_wayland_window == window) {
        if (!wl_list_empty(&compositor_state.wayland_windows)) {
            compositor_state.active_wayland_window = wl_container_of(
                compositor_state.wayland_windows.next, compositor_state.active_wayland_window, link);
            log_message(3, "Active Wayland window changed");
        } else {
            compositor_state.active_wayland_window = NULL;
            log_message(3, "No active Wayland windows remaining");
        }
    }
    
    // 清理窗口状态
    remove_wayland_window_state(window);
    
    // 释放资源
    free(listener);
    free(window);
}

// 处理Xwayland表面销毁事件
static void handle_surface_destroy(struct wl_listener* listener, void* data) {
    struct wlr_xwayland_surface* surface = data;
    const char* title = surface->title ? surface->title : "(unnamed)";
    
    log_message(2, "Surface destroyed: %s", title);
    
    // 从列表中移除
    if (!wl_list_empty(&surface->link)) {
        wl_list_remove(&surface->link);
    }
    
    // 如果销毁的是活动表面，选择下一个表面作为活动表面
    if (compositor_state.active_surface == surface) {
        if (!wl_list_empty(&compositor_state.surfaces)) {
            compositor_state.active_surface = wl_container_of(
                compositor_state.surfaces.next, compositor_state.active_surface, link);
            log_message(3, "Active surface changed to: %s", 
                        compositor_state.active_surface->title ? 
                        compositor_state.active_surface->title : "(unnamed)");
        } else {
            compositor_state.active_surface = NULL;
            log_message(3, "No active surfaces remaining");
        }
    }
    
    // 清理窗口状态
    remove_xwayland_window_state(surface);
    
    // 释放监听器
    free(listener);
}

static void handle_xwayland_new_surface(struct wl_listener *listener, void *data) {
    // 处理新的 X11 窗口
    struct wlr_xwayland_surface *xsurface = data;
    const char* title = xsurface->title ? xsurface->title : "(unnamed)";
    log_message(2, "New Xwayland surface: %s", title);
    
    // 将表面添加到管理列表
    wl_list_insert(&compositor_state.surfaces, &xsurface->link);
    
    // 初始化窗口状态
    XWaylandWindowState *window_state = get_xwayland_window_state(xsurface);
    if (window_state) {
        // 保存初始窗口位置和大小
        window_state->state.saved_x = xsurface->x;
        window_state->state.saved_y = xsurface->y;
        window_state->state.saved_width = xsurface->surface ? xsurface->surface->current.width : 0;
        window_state->state.saved_height = xsurface->surface ? xsurface->surface->current.height : 0;
        window_state->state.state = WINDOW_STATE_NORMAL;
    }
    
    // 默认将新表面设为活动表面
    if (!compositor_state.active_surface) {
        compositor_state.active_surface = xsurface;
        log_message(3, "Set active surface: %s", title);
    }
    
    // 监听表面销毁事件
    struct wl_listener* destroy_listener = calloc(1, sizeof(struct wl_listener));
    if (destroy_listener) {
        destroy_listener->notify = handle_surface_destroy;
        wl_signal_add(&xsurface->events.destroy, destroy_listener);
        // 将listener附加到surface以便在销毁时释放
        wl_list_init(&destroy_listener->link);
    }
}



int compositor_init(ANativeWindow* window, int width, int height, const CompositorConfig* config) {
    log_message(1, "Initializing compositor...");
    
    // 初始化全局状态
    memset(&compositor_state, 0, sizeof(compositor_state));
    compositor_state.window = window;
    compositor_state.width = width;
    compositor_state.height = height;
    compositor_state.epoll_fd = -1;
    compositor_state.last_error = COMPOSITOR_SUCCESS;
    
    // 初始化链表
    wl_list_init(&compositor_state.surfaces);
    wl_list_init(&compositor_state.xwayland_surfaces);
    
    // 使用默认配置或用户提供的配置
    if (config) {
        compositor_state.config = *config;
        log_message(2, "Using custom configuration");
        // 验证关键配置项
        if (compositor_state.config.max_swapchain_images < 1) {
            log_message(1, "Invalid max_swapchain_images value, using default");
            compositor_state.config.max_swapchain_images = 2;
        }
        if (compositor_state.config.preferred_refresh_rate < 1) {
            log_message(1, "Invalid preferred_refresh_rate value, using default");
            compositor_state.config.preferred_refresh_rate = 60;
        }
    } else {
        compositor_state.config = *compositor_get_default_config();
        log_message(2, "Using default configuration");
    }
    
    // 设置日志级别
    compositor_set_log_level(compositor_state.config.log_level);
    log_message(2, "Debug logging: %s", compositor_state.config.enable_debug_logging ? "enabled" : "disabled");
    
    // 初始化性能统计
    compositor_state.frame_count = 0;
    compositor_state.last_fps_time = time(NULL);
    compositor_state.current_fps = 0;
    
    log_message(1, "Initializing compositor with width=%d, height=%d", width, height);
    
    // 改进日志记录，输出完整配置信息
    log_message(2, "Configuration: Xwayland=%s, VSync=%s, MaxSwapchainImages=%d, PreferredRefreshRate=%dHz",
               compositor_state.config.enable_xwayland ? "enabled" : "disabled",
               compositor_state.config.enable_vsync ? "enabled" : "disabled",
               compositor_state.config.max_swapchain_images,
               compositor_state.config.preferred_refresh_rate);
    
    // 设置 wlroots 日志级别
    enum wlr_log_importance log_level = WLR_INFO;
    if (compositor_state.config.log_level >= 3) {
        log_level = WLR_DEBUG;
    } else if (compositor_state.config.log_level <= 0) {
        log_level = WLR_ERROR;
    }
    wlr_log_init(log_level, NULL);
    
    // 创建 Wayland 显示
    compositor_state.display = wl_display_create();
    if (!compositor_state.display) {
        set_error(COMPOSITOR_ERROR_WAYLAND, "Failed to create Wayland display");
        goto cleanup;
    }
    
    compositor_state.event_loop = wl_display_get_event_loop(compositor_state.display);
    
    // 创建 headless 后端
    compositor_state.backend = wlr_backend_autocreate(compositor_state.display, NULL);
    if (!compositor_state.backend) {
        set_error(COMPOSITOR_ERROR_WAYLAND, "Failed to create headless backend");
        goto cleanup;
    }
    
    // 创建渲染器
    compositor_state.renderer = wlr_backend_get_renderer(compositor_state.backend);
    if (!compositor_state.renderer) {
        set_error(COMPOSITOR_ERROR_WAYLAND, "Failed to get renderer");
        goto cleanup;
    }
    
    // 初始化渲染器
    wlr_renderer_init_wl_display(compositor_state.renderer, compositor_state.display);
    
    // 创建合成器
    compositor_state.compositor = wlr_compositor_create(compositor_state.display, 
        wlr_renderer_get_render_format(compositor_state.renderer), NULL);
    if (!compositor_state.compositor) {
        set_error(COMPOSITOR_ERROR_WAYLAND, "Failed to create compositor");
        goto cleanup;
    }
    
    // 创建子合成器
    compositor_state.subcompositor = wlr_subcompositor_create(compositor_state.display);
    if (!compositor_state.subcompositor) {
        set_error(COMPOSITOR_ERROR_WAYLAND, "Failed to create subcompositor");
        goto cleanup;
    }
    
    // 创建输出布局
    compositor_state.output_layout = wlr_output_layout_create();
    if (!compositor_state.output_layout) {
        set_error(COMPOSITOR_ERROR_WAYLAND, "Failed to create output layout");
        goto cleanup;
    }
    
    // 启动后端
    if (!wlr_backend_start(compositor_state.backend)) {
        set_error(COMPOSITOR_ERROR_WAYLAND, "Failed to start backend");
        goto cleanup;
    }
    
    // 获取输出
    struct wl_list *outputs = wlr_backend_get_outputs(compositor_state.backend);
    struct wlr_output *output;
    wl_list_for_each(output, outputs, link) {
        compositor_state.output = output;
        break;
    }
    
    if (!compositor_state.output) {
        set_error(COMPOSITOR_ERROR_WAYLAND, "No outputs found");
        goto cleanup;
    }
    
    // 配置输出
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_mode(&state, NULL);
    wlr_output_state_set_custom_mode(&state, width, height, compositor_state.config.preferred_refresh_rate * 1000);
    wlr_output_commit_state(compositor_state.output, &state);
    wlr_output_state_finish(&state);
    
    // 添加到输出布局
    wlr_output_layout_add_auto(compositor_state.output_layout, compositor_state.output);
    
    // 创建 Xwayland（如果启用）
    if (compositor_state.config.enable_xwayland) {
        compositor_state.xwayland = wlr_xwayland_create(compositor_state.display, compositor_state.compositor, true);
        if (!compositor_state.xwayland) {
            set_error(COMPOSITOR_ERROR_XWAYLAND, "Failed to create Xwayland");
            goto cleanup;
        }
        
        // 设置 Xwayland 事件监听器
        xwayland_ready_listener.notify = handle_xwayland_ready;
        xwayland_new_surface_listener.notify = handle_xwayland_new_surface;
        wl_signal_add(&compositor_state.xwayland->events.ready, &xwayland_ready_listener);
        wl_signal_add(&compositor_state.xwayland->events.new_surface, &xwayland_new_surface_listener);
    }
    
    // 设置新表面监听器
    new_surface_listener.notify = handle_new_surface;
    wl_signal_add(&compositor_state.compositor->events.new_surface, &new_surface_listener);
    
    // 初始化窗口管理链表
    wl_list_init(&compositor_state.surfaces);
    
    // 初始化 Vulkan
    if (!init_vulkan()) {
        goto cleanup;
    }
    
    // 启动 Xwayland 子进程（如果启用）
    if (compositor_state.config.enable_xwayland) {
        if (!start_xwayland()) {
            goto cleanup;
        }
    } else {
        log_message(2, "Xwayland is disabled by configuration");
    }
    
    // 创建 epoll 实例
    compositor_state.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (compositor_state.epoll_fd == -1) {
        set_error(COMPOSITOR_ERROR_SYSTEM, "Failed to create epoll: %s", strerror(errno));
        goto cleanup;
    }
    
    // 添加 Wayland 显示到 epoll
    int display_fd = wl_display_get_fd(compositor_state.display);
    compositor_state.display_event.events = EPOLLIN;
    compositor_state.display_event.data.fd = display_fd;
    if (epoll_ctl(compositor_state.epoll_fd, EPOLL_CTL_ADD, display_fd, &compositor_state.display_event) == -1) {
        set_error(COMPOSITOR_ERROR_SYSTEM, "Failed to add display to epoll: %s", strerror(errno));
        goto cleanup;
    }
    
    compositor_state.initialized = true;
    compositor_state.running = true;
    log_message(1, "Compositor initialized successfully");
    return 0;
    
cleanup:
    compositor_destroy();
    return -1;
}

// 更新FPS计数器
static void update_fps_counter() {
    compositor_state.frame_count++;
    time_t current_time = time(NULL);
    
    if (current_time - compositor_state.last_fps_time >= 1) {
        compositor_state.current_fps = compositor_state.frame_count;
        compositor_state.frame_count = 0;
        compositor_state.last_fps_time = current_time;
        
        if (compositor_state.config.enable_debug_logging) {
            log_message(3, "FPS: %d", compositor_state.current_fps);
        }
    }
}

int compositor_step() {
    if (!compositor_state.initialized || !compositor_state.running) {
        set_error(COMPOSITOR_ERROR_INVALID_STATE, "Compositor not initialized or stopped");
        return -1;
    }
    
    // 处理 Wayland 事件
    struct epoll_event events[16];
    int nfds = epoll_wait(compositor_state.epoll_fd, events, 16, 0);
    if (nfds == -1) {
        if (errno != EINTR) {
            set_error(COMPOSITOR_ERROR_SYSTEM, "epoll_wait failed: %s", strerror(errno));
        }
        return 0;
    }
    
    for (int i = 0; i < nfds; i++) {
        if (events[i].data.fd == wl_display_get_fd(compositor_state.display)) {
            int ret = wl_display_dispatch(compositor_state.display);
            if (ret < 0) {
                set_error(COMPOSITOR_ERROR_WAYLAND, "Failed to dispatch Wayland events: %d", ret);
            }
        }
    }
    
    // 发送帧事件
    if (compositor_state.output) {
        wlr_output_schedule_frame(compositor_state.output);
        int ret = wl_display_flush_clients(compositor_state.display);
        if (ret < 0) {
            log_message(2, "Failed to flush Wayland clients: %d", ret);
        }
    }
    
    // 执行 Vulkan 渲染和呈现
    if (!render_frame()) {
        return -1;
    }
    
    // 更新性能统计
    update_fps_counter();
    
    return 0;
}

// 渲染窗口装饰器
static void render_window_decoration(struct wlr_renderer *renderer, int x, int y, int width, bool is_active) {
    // 渲染标题栏背景
    struct wlr_box titlebar = {
        .x = x,
        .y = y,
        .width = width,
        .height = 30 // 标题栏高度
    };
    
    // 活动窗口使用不同的颜色
    float color[4] = {0.2f, 0.2f, 0.2f, 1.0f}; // 非活动窗口灰色
    if (is_active) {
        color[0] = 0.3f; // 活动窗口深蓝色
        color[1] = 0.5f;
        color[2] = 0.7f;
    }
    
    wlr_render_rect(renderer, &titlebar, color, NULL);
    
    // 渲染边框
    struct wlr_box border = {
        .x = x,
        .y = y,
        .width = width,
        .height = 1 // 边框线宽度
    };
    
    float border_color[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    
    // 上边框
    wlr_render_rect(renderer, &border, border_color, NULL);
    
    // 左边框
    border.y = y + 1;
    border.width = 1;
    border.height = 30 - 1;
    wlr_render_rect(renderer, &border, border_color, NULL);
    
    // 右边框
    border.x = x + width - 1;
    wlr_render_rect(renderer, &border, border_color, NULL);
}

// 检查窗口是否在可见区域内
static bool is_window_visible(int x, int y, int width, int height, int screen_width, int screen_height) {
    // 快速检查窗口是否完全在屏幕外
    if (x >= screen_width || y >= screen_height || 
        x + width <= 0 || y + height <= 0) {
        return false;
    }
    return true;
}

// 标记脏区域
static void mark_dirty_rect(int x, int y, int width, int height) {
    if (!compositor_state.initialized) {
        return;
    }
    
    if (!compositor_state.use_dirty_rect) {
        compositor_state.full_redraw_needed = true;
        return;
    }
    
    // 限制在屏幕范围内
    x = MAX(x, 0);
    y = MAX(y, 0);
    width = MIN(width, compositor_state.width - x);
    height = MIN(height, compositor_state.height - y);
    
    if (width <= 0 || height <= 0) {
        return;
    }
    
    // 验证参数
    if (width <= 0 || height <= 0 || 
        x < 0 || y < 0 || 
        x >= compositor_state.width || 
        y >= compositor_state.height) {
        log_message(1, "Invalid dirty rect coordinates: %d,%d,%d,%d", x, y, width, height);
        return;
    }
    
    // 限制在屏幕范围内
    width = MIN(width, compositor_state.width - x);
    height = MIN(height, compositor_state.height - y);
    
    // 使用compositor_state中已有的full_redraw_needed标志
    compositor_state.full_redraw_needed = true;
    
    // 更新脏区域的最小/最大坐标
    if (x < compositor_state.dirty_rect.x || compositor_state.dirty_rect.width == 0) {
        compositor_state.dirty_rect.x = x;
    }
    if (y < compositor_state.dirty_rect.y || compositor_state.dirty_rect.height == 0) {
        compositor_state.dirty_rect.y = y;
    }
    if (x + width > compositor_state.dirty_rect.x + compositor_state.dirty_rect.width || compositor_state.dirty_rect.width == 0) {
        compositor_state.dirty_rect.width = x + width - compositor_state.dirty_rect.x;
    }
    if (y + height > compositor_state.dirty_rect.y + compositor_state.dirty_rect.height || compositor_state.dirty_rect.height == 0) {
        compositor_state.dirty_rect.height = y + height - compositor_state.dirty_rect.y;
    }
    
    // 检查是否需要全屏重绘（如果脏区域超过屏幕的80%）
    int screen_area = compositor_state.width * compositor_state.height;
    int dirty_area = compositor_state.dirty_rect.width * compositor_state.dirty_rect.height;
    
    if (dirty_area > screen_area * 0.8) {
        log_message(2, "Dirty area exceeds 80%% of screen, triggering full redraw");
        compositor_state.dirty_rect.x = 0;
        compositor_state.dirty_rect.y = 0;
        compositor_state.dirty_rect.width = compositor_state.width;
        compositor_state.dirty_rect.height = compositor_state.height;
    }
    
    log_message(3, "Dirty rect updated to: %d,%d,%d,%d (area: %d/%d)", 
                compositor_state.dirty_rect.x, compositor_state.dirty_rect.y,
                compositor_state.dirty_rect.width, compositor_state.dirty_rect.height,
                dirty_area, screen_area);
}

// 重置脏区域
static void reset_dirty_rect() {
    if (!compositor_state.initialized) {
        return;
    }
    
    // 重置脏区域标志
    compositor_state.full_redraw_needed = false;
    
    // 清空脏区域矩形
    compositor_state.dirty_rect.x = 0;
    compositor_state.dirty_rect.y = 0;
    compositor_state.dirty_rect.width = 0;
    compositor_state.dirty_rect.height = 0;
    
    log_message(4, "Dirty rect reset");
}

// 标记整个屏幕为脏
static void mark_full_redraw() {
    compositor_state.full_redraw_needed = true;
}

// 渲染单帧
static bool render_frame() {
    // 检查设备有效性
    if (!compositor_state.device) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Invalid device in render_frame");
        return false;
    }
    
    // 等待前一帧完成
    if (vkWaitForFences(compositor_state.device, 1, &compositor_state.in_flight_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to wait for fence");
        return false;
    }
    
    // 重置fence
    if (vkResetFences(compositor_state.device, 1, &compositor_state.in_flight_fence) != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to reset fence");
        return false;
    }
    
    // 获取下一个图像
    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(
        compositor_state.device,
        compositor_state.swapchain,
        compositor_state.config.enable_vsync ? UINT64_MAX : 0, // 如果启用垂直同步，则无限等待
        compositor_state.image_available_semaphore,
        VK_NULL_HANDLE,
        &image_index
    );
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // 需要重建交换链
        log_message(2, "Swapchain needs recreation");
        if (!recreate_swapchain()) {
            return false;
        }
        return true;
    } else if (result != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to acquire next image: %d", result);
        return false;
    }
    
    // 记录渲染活动窗口信息
    if (compositor_state.active_surface) {
        log_message(3, "Rendering active surface: %s", 
                   compositor_state.active_surface->title ? 
                   compositor_state.active_surface->title : "(unnamed)");
    }
    
    // 合成所有窗口表面
    // 使用wlroots的渲染器进行硬件加速合成
    if (compositor_state.renderer && compositor_state.output) {
        // 开始渲染
        if (!wlr_renderer_begin(compositor_state.renderer, compositor_state.width, compositor_state.height)) {
            log_message(1, "Failed to begin renderer");
            return false;
        }
        
        // 清除背景为配置的背景色
        float bg_color[4] = {compositor_state.config.background_color[0], 
                             compositor_state.config.background_color[1], 
                             compositor_state.config.background_color[2], 1.0f};
        
        // 如果需要完全重绘或脏区域有效，则清除背景
        if (compositor_state.full_redraw_needed) {
            wlr_renderer_clear(compositor_state.renderer, bg_color);
        } else if (compositor_state.use_dirty_rect && 
                  compositor_state.dirty_rect.width > 0 && 
                  compositor_state.dirty_rect.height > 0) {
            // 只清除脏区域
            wlr_renderer_scissor(compositor_state.renderer, &compositor_state.dirty_rect);
            wlr_renderer_clear(compositor_state.renderer, bg_color);
            wlr_renderer_scissor(compositor_state.renderer, NULL);
        }
        
        // 应用输出变换
        enum wl_output_transform transform = wlr_output_transform_invert(
            compositor_state.output->transform);
        
        // 遍历所有窗口表面进行渲染
        if (!wl_list_empty(&compositor_state.surfaces)) {
            // 从链表尾部开始渲染（底层窗口）
            struct wl_list *pos;
            wl_list_for_each_reverse(pos, &compositor_state.surfaces, link) {
                bool is_xwayland = true;
                int window_x = 0, window_y = 0, window_width = 0, window_height = 0;
                struct wlr_texture *texture = NULL;
                bool is_active = false;
                const char *title = NULL;
                
                // 尝试识别为Xwayland表面
                struct wlr_xwayland_surface *xsurface = wl_container_of(pos, xsurface, link);
                if (xsurface && xsurface->surface && xsurface->mapped && xsurface->surface->texture) {
                    // 有效Xwayland表面
                    window_x = xsurface->x;
                    window_y = xsurface->y;
                    window_width = xsurface->surface->current->width;
                    window_height = xsurface->surface->current->height;
                    texture = xsurface->surface->texture;
                    is_active = (compositor_state.active_surface == xsurface);
                    title = xsurface->title;
                    is_xwayland = true;
                } else {
                    // 尝试识别为WaylandWindow
                    WaylandWindow *window = wl_container_of(pos, window, link);
                    if (window && window->surface && window->mapped && window->surface->texture) {
                        // 有效Wayland窗口
                        window_x = window->x;
                        window_y = window->y;
                        window_width = window->width;
                        window_height = window->height;
                        texture = window->surface->texture;
                        is_active = false; // Wayland窗口暂时不支持活动状态
                        title = window->title;
                        is_xwayland = false;
                    } else {
                        // 无法识别的类型，跳过
                        continue;
                    }
                }
                
                // 检查窗口是否在可见区域内
                if (!is_window_visible(window_x, window_y, window_width, window_height, 
                                      compositor_state.width, compositor_state.height)) {
                    continue;
                }
                
                // 检查窗口是否与脏区域相交（如果启用了脏区域渲染）
                bool should_render = true;
                if (compositor_state.use_dirty_rect && !compositor_state.full_redraw_needed) {
                    // 检查窗口是否与脏区域相交
                    bool intersects = !(window_x + window_width < compositor_state.dirty_rect.x ||
                                      window_x > compositor_state.dirty_rect.x + compositor_state.dirty_rect.width ||
                                      window_y + (compositor_state.config.enable_window_decoration ? 30 : 0) + window_height < compositor_state.dirty_rect.y ||
                                      window_y > compositor_state.dirty_rect.y + compositor_state.dirty_rect.height);
                    
                    if (!intersects) {
                        continue; // 窗口不与脏区域相交，跳过渲染
                    }
                }
                
                // 应用窗口装饰器（如果启用）
                int render_y = window_y;
                if (compositor_state.config.enable_window_decoration) {
                    render_window_decoration(compositor_state.renderer, window_x, window_y, 
                                           window_width, is_active);
                    render_y += 30; // 标题栏高度
                }
                
                // 创建渲染框
                struct wlr_box box = {
                    .x = window_x,
                    .y = render_y,
                    .width = window_width,
                    .height = window_height
                };
                
                // 渲染窗口内容
                if (texture) {
                    // 如果启用了脏区域渲染，设置裁剪区域
                    if (compositor_state.use_dirty_rect && !compositor_state.full_redraw_needed) {
                        // 创建与脏区域相交的裁剪框
                        struct wlr_box scissor_box = {
                            .x = MAX(box.x, compositor_state.dirty_rect.x),
                            .y = MAX(box.y, compositor_state.dirty_rect.y),
                            .width = MIN(box.width, compositor_state.dirty_rect.x + compositor_state.dirty_rect.width - scissor_box.x),
                            .height = MIN(box.height, compositor_state.dirty_rect.y + compositor_state.dirty_rect.height - scissor_box.y)
                        };
                        
                        if (scissor_box.width > 0 && scissor_box.height > 0) {
                            wlr_renderer_scissor(compositor_state.renderer, &scissor_box);
                        } else {
                            continue; // 无交集，跳过渲染
                        }
                    }
                    
                    // 渲染窗口内容
                    wlr_render_texture_with_matrix(
                        compositor_state.renderer,
                        texture,
                        transform,
                        &box,
                        NULL
                    );
                    
                    // 清除裁剪区域设置
                    if (compositor_state.use_dirty_rect && !compositor_state.full_redraw_needed) {
                        wlr_renderer_scissor(compositor_state.renderer, NULL);
                    }
                    
                    log_message(3, "Rendered %s window '%s' at %d,%d: %dx%d", 
                               is_xwayland ? "Xwayland" : "Wayland",
                               title ? title : "(unnamed)",
                               window_x, window_y, window_width, window_height);
                }
            }
        }
        
        // 结束渲染并提交到输出
        wlr_renderer_end(compositor_state.renderer);
        
        // 提交渲染结果到输出
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        
        if (!wlr_output_attach_render(compositor_state.output, NULL)) {
            log_message(1, "Failed to attach render to output");
            return false;
        }
        
        // 渲染软件光标（如果有）
        wlr_output_render_software_cursors(compositor_state.output, NULL);
        
        // 提交输出
        if (!wlr_output_commit(compositor_state.output)) {
            log_message(1, "Failed to commit output");
            // 提交失败通常意味着需要重建交换链
            recreate_swapchain();
        }
    }
    
    // 注意：当使用wlroots渲染器时，不需要手动执行Vulkan提交操作
    // wlr_output_commit已经处理了缓冲区的提交和呈现
    
    // 重置脏区域，准备下一帧
        reset_dirty_rect();
        
        // 验证呈现结果
        if (compositor_state.config.enable_debug_logging) {
            log_message(3, "Frame rendered successfully");
        }
        
        return true;
}

// 重建交换链（用于窗口大小变化时）
static bool recreate_swapchain() {
    // 检查设备是否有效
    if (compositor_state.device == VK_NULL_HANDLE) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Invalid device for swapchain recreation");
        return false;
    }
    
    // 等待设备空闲
    VkResult result = vkDeviceWaitIdle(compositor_state.device);
    if (result != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to wait for device idle: %d", result);
        return false;
    }
    
    // 保存旧的交换链
    VkSwapchainKHR old_swapchain = compositor_state.swapchain;
    
    // 重新获取表面能力
    VkSurfaceCapabilitiesKHR capabilities;
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        compositor_state.physical_device, 
        compositor_state.surface, 
        &capabilities
    );
    
    if (result != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to get surface capabilities: %d", result);
        return false;
    }
    
    // 设置新的交换链扩展
    compositor_state.swapchain_extent.width = compositor_state.width;
    compositor_state.swapchain_extent.height = compositor_state.height;
    
    // 确保交换链范围在有效范围内
    if (compositor_state.swapchain_extent.width < capabilities.minImageExtent.width) {
        compositor_state.swapchain_extent.width = capabilities.minImageExtent.width;
    }
    if (capabilities.maxImageExtent.width > 0 && 
        compositor_state.swapchain_extent.width > capabilities.maxImageExtent.width) {
        compositor_state.swapchain_extent.width = capabilities.maxImageExtent.width;
    }
    
    if (compositor_state.swapchain_extent.height < capabilities.minImageExtent.height) {
        compositor_state.swapchain_extent.height = capabilities.minImageExtent.height;
    }
    if (capabilities.maxImageExtent.height > 0 && 
        compositor_state.swapchain_extent.height > capabilities.maxImageExtent.height) {
        compositor_state.swapchain_extent.height = capabilities.maxImageExtent.height;
    }
    
    log_message(2, "Recreating swapchain with extent: %ux%u", 
                compositor_state.swapchain_extent.width, 
                compositor_state.swapchain_extent.height);
    
    // 确定交换链图像数量
    uint32_t image_count = compositor_state.config.max_swapchain_images;
    if (image_count == 0) {
        // 如果未设置最大图像数量，则使用默认值
        image_count = capabilities.minImageCount + 1;
    }
    
    if (image_count < capabilities.minImageCount) {
        image_count = capabilities.minImageCount;
    }
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }
    
    // 选择最佳的呈现模式
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR; // 默认安全模式
    
    // 获取可用的呈现模式
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        compositor_state.physical_device, 
        compositor_state.surface, 
        &present_mode_count, 
        NULL
    );
    
    if (present_mode_count > 0) {
        VkPresentModeKHR *present_modes = (VkPresentModeKHR*)malloc(
            sizeof(VkPresentModeKHR) * present_mode_count
        );
        
        if (present_modes) {
            if (vkGetPhysicalDeviceSurfacePresentModesKHR(
                    compositor_state.physical_device, 
                    compositor_state.surface, 
                    &present_mode_count, 
                    present_modes
                ) == VK_SUCCESS) {
                
                // 根据配置选择呈现模式
                if (compositor_state.config.enable_vsync) {
                    // 启用垂直同步时，优先选择FIFO模式（始终可用）
                    present_mode = VK_PRESENT_MODE_FIFO_KHR;
                } else {
                    // 禁用垂直同步时，优先选择MAILBOX或IMMEDIATE模式
                    for (uint32_t i = 0; i < present_mode_count; i++) {
                        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                            break;
                        } else if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                            present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                        }
                    }
                }
            }
            free(present_modes);
        }
    }
    
    // 创建新的交换链
    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = compositor_state.surface,
        .minImageCount = image_count,
        .imageFormat = compositor_state.swapchain_format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = compositor_state.swapchain_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain
    };
    
    result = vkCreateSwapchainKHR(compositor_state.device, &swapchain_create_info, NULL, &compositor_state.swapchain);
    if (result != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to create new swapchain: %d", result);
        // 恢复旧的交换链
        compositor_state.swapchain = old_swapchain;
        return false;
    }
    
    // 销毁旧的交换链（在创建新交换链之后）
    if (old_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(compositor_state.device, old_swapchain, NULL);
    }
    
    // 获取新的交换链图像
    result = vkGetSwapchainImagesKHR(
        compositor_state.device, 
        compositor_state.swapchain, 
        &compositor_state.swapchain_image_count, 
        NULL
    );
    
    if (result != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to get swapchain images count: %d", result);
        return false;
    }
    
    // 释放旧的资源
    if (compositor_state.swapchain_images) {
        free(compositor_state.swapchain_images);
        compositor_state.swapchain_images = NULL;
    }
    
    if (compositor_state.swapchain_image_views) {
        for (uint32_t i = 0; i < compositor_state.swapchain_image_count; i++) {
            if (compositor_state.swapchain_image_views[i]) {
                vkDestroyImageView(compositor_state.device, compositor_state.swapchain_image_views[i], NULL);
            }
        }
        free(compositor_state.swapchain_image_views);
        compositor_state.swapchain_image_views = NULL;
    }
    
    // 分配新的图像数组
    compositor_state.swapchain_images = (VkImage*)malloc(
        sizeof(VkImage) * compositor_state.swapchain_image_count
    );
    
    if (!compositor_state.swapchain_images) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for new swapchain images");
        return false;
    }
    
    result = vkGetSwapchainImagesKHR(
        compositor_state.device, 
        compositor_state.swapchain, 
        &compositor_state.swapchain_image_count, 
        compositor_state.swapchain_images
    );
    
    if (result != VK_SUCCESS) {
        set_error(COMPOSITOR_ERROR_VULKAN, "Failed to get swapchain images: %d", result);
        free(compositor_state.swapchain_images);
        compositor_state.swapchain_images = NULL;
        return false;
    }
    
    // 创建新的图像视图数组
    compositor_state.swapchain_image_views = (VkImageView*)malloc(
        sizeof(VkImageView) * compositor_state.swapchain_image_count
    );
    
    if (!compositor_state.swapchain_image_views) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for new swapchain image views");
        free(compositor_state.swapchain_images);
        compositor_state.swapchain_images = NULL;
        return false;
    }
    
    // 初始化图像视图数组
    for (uint32_t i = 0; i < compositor_state.swapchain_image_count; i++) {
        compositor_state.swapchain_image_views[i] = VK_NULL_HANDLE;
    }
    
    // 创建新的图像视图
    for (uint32_t i = 0; i < compositor_state.swapchain_image_count; i++) {
        VkImageViewCreateInfo view_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = compositor_state.swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = compositor_state.swapchain_format,
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

        result = vkCreateImageView(
            compositor_state.device, 
            &view_create_info, 
            NULL, 
            &compositor_state.swapchain_image_views[i]
        );
        
        if (result != VK_SUCCESS) {
            set_error(COMPOSITOR_ERROR_VULKAN, "Failed to create new image view %u: %d", i, result);
            
            // 清理已创建的图像视图
            for (uint32_t j = 0; j < i; j++) {
                if (compositor_state.swapchain_image_views[j]) {
                    vkDestroyImageView(compositor_state.device, compositor_state.swapchain_image_views[j], NULL);
                }
            }
            
            free(compositor_state.swapchain_image_views);
            compositor_state.swapchain_image_views = NULL;
            free(compositor_state.swapchain_images);
            compositor_state.swapchain_images = NULL;
            return false;
        }
    }
    
    log_message(2, "Swapchain recreated successfully with %u images, present mode: %u", 
                compositor_state.swapchain_image_count, present_mode);
    return true;
}



void compositor_destroy() {
    log_message(1, "Destroying compositor...");
    
    compositor_state.running = false;
    
    // 停止 Xwayland
    stop_xwayland();
    
    // 清理 epoll
    if (compositor_state.epoll_fd != -1) {
        close(compositor_state.epoll_fd);
        compositor_state.epoll_fd = -1;
    }
    
    // 清理 Vulkan
    cleanup_vulkan();
    
    // 清理事件监听器
    // 移除 Xwayland 事件监听器
    if (compositor_state.xwayland) {
        wl_list_remove(&xwayland_ready_listener.link);
        wl_list_remove(&xwayland_new_surface_listener.link);
    }
    
    // 移除新表面事件监听器
    if (compositor_state.compositor) {
        wl_list_remove(&new_surface_listener.link);
    }
    
    // 清理窗口管理资源
    // 销毁所有活动的表面
    struct wlr_xwayland_surface *xsurface, *tmp;
    wl_list_for_each_safe(xsurface, tmp, &compositor_state.surfaces, link) {
        // 从列表中移除
        wl_list_remove(&xsurface->link);
        
        // 销毁表面
        if (xsurface->surface) {
            wl_resource_destroy(xsurface->surface->resource);
        }
    }
    
    // 清空表面列表
    wl_list_init(&compositor_state.surfaces);
    
    // 清理 wlroots 资源（按正确的顺序）
    if (compositor_state.xwayland) {
        wlr_xwayland_destroy(compositor_state.xwayland);
        compositor_state.xwayland = NULL;
    }
    
    if (compositor_state.output_layout) {
        wlr_output_layout_destroy(compositor_state.output_layout);
        compositor_state.output_layout = NULL;
    }
    
    // 从输出布局中移除输出
    if (compositor_state.output) {
        if (compositor_state.output_layout) {
            wlr_output_layout_remove(compositor_state.output_layout, compositor_state.output);
        }
        // 输出会在后端销毁时自动清理
        compositor_state.output = NULL;
    }
    
    if (compositor_state.subcompositor) {
        wlr_subcompositor_destroy(compositor_state.subcompositor);
        compositor_state.subcompositor = NULL;
    }
    
    if (compositor_state.compositor) {
        wlr_compositor_destroy(compositor_state.compositor);
        compositor_state.compositor = NULL;
    }
    
    if (compositor_state.backend) {
        wlr_backend_destroy(compositor_state.backend);
        compositor_state.backend = NULL;
    }
    
    if (compositor_state.display) {
        wl_display_destroy(compositor_state.display);
        compositor_state.display = NULL;
    }
    
    // 重置状态
    memset(&compositor_state, 0, sizeof(compositor_state));
    compositor_state.epoll_fd = -1;
    compositor_state.initialized = false;

    // 初始化链表
    wl_list_init(&compositor_state.surfaces);
    wl_list_init(&compositor_state.xwayland_surfaces);

    // 重置事件监听器
    memset(&xwayland_ready_listener, 0, sizeof(xwayland_ready_listener));
    memset(&xwayland_new_surface_listener, 0, sizeof(xwayland_new_surface_listener));
    memset(&new_surface_listener, 0, sizeof(new_surface_listener));
    
    log_message(1, "Compositor destroyed successfully");
}

// 检查点是否在Wayland窗口内
static bool is_point_in_wayland_window(WaylandWindow *window, int x, int y) {
    if (!window || !window->mapped || !window->surface) {
        return false;
    }
    // 确保width和height有效
    if (window->width <= 0 || window->height <= 0) {
        return false;
    }
    return (x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height);
}

// 检查点是否在窗口内（公共接口实现）
bool is_point_in_window(void* surface, int x, int y) {
    if (!surface) {
        return false;
    }
    
    // 更安全的方法：搜索链表来确定表面类型
    // 首先检查是否在Xwayland窗口链表中
    struct wlr_xwayland_surface *xsurface;
    wl_list_for_each(xsurface, &compositor_state.xwayland_surfaces, link) {
        if (xsurface == surface) {
            return is_point_in_xwayland_surface(xsurface, x, y);
        }
    }
    
    // 然后检查是否在Wayland窗口链表中
    WaylandWindow *window;
    wl_list_for_each(window, &compositor_state.surfaces, link) {
        if (window == surface) {
            if (!window || !window->mapped || !window->surface) {
                return false;
            }
            // 确保width和height有效
            if (window->width <= 0 || window->height <= 0) {
                return false;
            }
            return (x >= window->x && x < window->x + window->width &&
                    y >= window->y && y < window->y + window->height);
        }
    }
    
    // 不是已知的窗口类型
    return false;
}

// 检查点是否在Xwayland窗口内
static bool is_point_in_xwayland_surface(struct wlr_xwayland_surface *surface, int x, int y) {
    if (!surface || !surface->surface) {
        return false;
    }
    return (x >= surface->x && x < surface->x + surface->surface->current.width &&
            y >= surface->y && y < surface->y + surface->surface->current.height);
}

// 高效查找Xwayland窗口
static struct wlr_xwayland_surface* find_xwayland_surface_by_title(const char* window_title) {
    if (!window_title) {
        return NULL;
    }
    
    struct wlr_xwayland_surface *surface;
    wl_list_for_each(surface, &compositor_state.xwayland_surfaces, link) {
        if (surface->title && strcmp(surface->title, window_title) == 0) {
            return surface;
        }
    }
    return NULL;
}

// 高效查找Wayland窗口
static WaylandWindow* find_wayland_window_by_title(const char* window_title) {
    if (!window_title) {
        return NULL;
    }
    
    struct wl_list *pos;
    wl_list_for_each(pos, &compositor_state.surfaces, link) {
        // 尝试识别为WaylandWindow
        WaylandWindow *window = wl_container_of(pos, window, link);
        if (window && window->title && strcmp(window->title, window_title) == 0) {
            return window;
        }
    }
    return NULL;
}

// 检查点是否在窗口装饰器区域
static bool is_point_in_decoration(void *surface, bool is_xwayland, int x, int y) {
    if (is_xwayland) {
        struct wlr_xwayland_surface *xsurface = surface;
        if (!xsurface || !xsurface->surface) {
            return false;
        }
        // 装饰器区域假设在窗口顶部 30 像素
        return (x >= xsurface->x && x < xsurface->x + xsurface->surface->current.width &&
                y >= xsurface->y && y < xsurface->y + 30);
    } else {
        WaylandWindow *window = surface;
        if (!window || !window->mapped) {
            return false;
        }
        // 装饰器区域假设在窗口顶部 30 像素
        return (x >= window->x && x < window->x + window->width &&
                y >= window->y && y < window->y + 30);
    }
}

// 从链表中查找包含给定点的窗口
static void *find_surface_at_position(int x, int y, bool *is_xwayland) {
    if (!compositor_state.initialized) {
        return NULL;
    }
    
    // 先检查 Xwayland 窗口
    struct wlr_xwayland_surface *surface;
    wl_list_for_each_reverse(surface, &compositor_state.xwayland_surfaces, link) {
        if (surface && is_point_in_xwayland_surface(surface, x, y)) {
            if (is_xwayland) {
                *is_xwayland = true;
            }
            return surface;
        }
    }
    
    // 再检查 Wayland 窗口
    WaylandWindow *window;
    wl_list_for_each_reverse(window, &compositor_state.surfaces, link) {
        if (window && is_point_in_wayland_window(window, x, y)) {
            if (is_xwayland) {
                *is_xwayland = false;
            }
            return window;
        }
    }
    
    return NULL;
}

// 全局变量，用于跟踪Alt键状态
static bool g_alt_key_pressed = false;

void compositor_handle_input(int type, int x, int y, int key, int state) {
    static bool dragging = false;
    static int drag_offset_x = 0, drag_offset_y = 0;
    static void *dragged_surface = NULL;
    static bool dragged_is_xwayland = false;
    
    if (!compositor_state.initialized) {
        log_message(2, "Input event ignored: compositor not initialized");
        return;
    }
    
    // 记录输入事件（调试模式）
    if (compositor_state.config.enable_debug_logging) {
        const char* type_str = "UNKNOWN";
        const char* state_str = "UNKNOWN";
        
        switch (type) {
            case COMPOSITOR_INPUT_MOTION:
                type_str = "MOTION";
                break;
            case COMPOSITOR_INPUT_BUTTON:
                type_str = "BUTTON";
                break;
            case COMPOSITOR_INPUT_KEY:
                type_str = "KEY";
                break;
            case COMPOSITOR_INPUT_TOUCH:
                type_str = "TOUCH";
                break;
        }
        
        switch (state) {
            case COMPOSITOR_INPUT_STATE_UP:
                state_str = "UP";
                break;
            case COMPOSITOR_INPUT_STATE_DOWN:
                state_str = "DOWN";
                break;
            case COMPOSITOR_INPUT_STATE_MOVE:
                state_str = "MOVE";
                break;
        }
        
        log_message(3, "Input event: %s, x=%d, y=%d, key=%d, state=%s", 
                   type_str, x, y, key, state_str);
    }
    
    // 处理拖拽
    if (dragging) {
        if (type == COMPOSITOR_INPUT_MOTION) {
            // 更新窗口位置，确保窗口不会移出屏幕边界
            int new_x = x - drag_offset_x;
            int new_y = y - drag_offset_y;
            
            if (dragged_is_xwayland) {
                struct wlr_xwayland_surface *surface = dragged_surface;
                if (surface) {
                    // 边界检查，允许窗口部分超出屏幕
                    surface->x = new_x;
                    surface->y = new_y;
                    
                    // 如果配置了窗口边缘吸附，可以在这里添加吸附逻辑
                }
            } else {
                WaylandWindow *window = dragged_surface;
                if (window) {
                    window->x = new_x;
                    window->y = new_y;
                }
            }
            return;
        } else if (type == COMPOSITOR_INPUT_BUTTON && state == COMPOSITOR_INPUT_STATE_UP) {
            // 结束拖拽
            dragging = false;
            dragged_surface = NULL;
            log_message(3, "Window drag finished");
            return;
        }
    }
    
    // 处理输入事件
    switch (type) {
        case COMPOSITOR_INPUT_MOTION:
            // 处理鼠标移动
            // 检查鼠标是否在窗口上方
            bool is_xwayland = false;
            void *surface = find_surface_at_position(x, y, &is_xwayland);
            
            if (surface) {
                // 可以在这里添加鼠标悬停效果、光标形状变化等
                // TODO: 实现鼠标悬停效果
            }
            break;
            
        case COMPOSITOR_INPUT_BUTTON:
            if (state == COMPOSITOR_INPUT_STATE_DOWN) {
                bool is_xwayland = false;
                void *surface = find_surface_at_position(x, y, &is_xwayland);
                
                if (surface) {
                    // 检查是否点击在标题栏区域
                    if (is_point_in_decoration(surface, is_xwayland, x, y)) {
                        // 开始拖拽
                        dragging = true;
                        dragged_surface = surface;
                        dragged_is_xwayland = is_xwayland;
                        
                        if (is_xwayland) {
                            struct wlr_xwayland_surface *xsurface = surface;
                            drag_offset_x = x - xsurface->x;
                            drag_offset_y = y - xsurface->y;
                        } else {
                            WaylandWindow *window = surface;
                            drag_offset_x = x - window->x;
                            drag_offset_y = y - window->y;
                        }
                        log_message(3, "Started dragging window");
                    } else {
                        // 点击窗口内容区域，切换活动窗口
                        if (is_xwayland) {
                            struct wlr_xwayland_surface *xsurface = surface;
                            if (xsurface && compositor_state.active_surface != xsurface) {
                                // 将活动表面移到链表前面
                                wl_list_remove(&xsurface->link);
                                wl_list_insert(&compositor_state.xwayland_surfaces, &xsurface->link);
                                compositor_state.active_surface = xsurface;
                                log_message(3, "Activated Xwayland surface: %s", 
                                          xsurface->title ? xsurface->title : "(unnamed)");
                            }
                            
                            // 转发鼠标点击事件到Xwayland窗口
                            if (compositor_state.xwayland && xsurface) {
                                // 计算相对坐标
                                double surface_x = x - xsurface->x;
                                double surface_y = y - xsurface->y;
                                // 创建并发送点击事件
                                // TODO: 实现具体的事件转发逻辑
                                log_message(3, "Forwarded mouse click to Xwayland surface: x=%f, y=%f", 
                                          surface_x, surface_y);
                            }
                        } else {
                            WaylandWindow *window = surface;
                            // Wayland窗口的激活逻辑
                            if (window) {
                                log_message(3, "Activated Wayland window: %s", 
                                          window->title ? window->title : "(unnamed)");
                                // 转发鼠标点击事件到Wayland窗口
                                // TODO: 实现具体的事件转发逻辑
                            }
                        }
                    }
                }
            }
            break;
            
        case COMPOSITOR_INPUT_KEY:
            // 处理键盘事件
            // 检查是否是Alt+F4关闭窗口快捷键
            if (key == 59) {  // Alt键
                if (state == COMPOSITOR_INPUT_STATE_DOWN) {
                    g_alt_key_pressed = true;
                    log_message(3, "Alt key pressed");
                } else if (state == COMPOSITOR_INPUT_STATE_UP) {
                    g_alt_key_pressed = false;
                    log_message(3, "Alt key released");
                }
            } else if (key == 65 && state == COMPOSITOR_INPUT_STATE_DOWN) {  // F4键
                if (g_alt_key_pressed && compositor_state.active_surface) {
                    // 调用关闭窗口函数
                    compositor_close_window(compositor_get_active_window_title());
                    log_message(2, "Alt+F4 pressed, closed active window");
                }
            } else if (compositor_state.active_surface) {
                // 转发其他键盘事件到活动窗口
                log_message(3, "Key event sent to active surface: key=%d, state=%d", key, state);
                
                // 转发键盘事件到活动窗口
                if (compositor_state.active_surface) {
                    // TODO: 实现键盘事件转发逻辑
                }
            } else {
                log_message(3, "No active surface for key event");
            }
            break;
            
        case COMPOSITOR_INPUT_TOUCH:
            // 处理触摸事件（映射到鼠标事件）
            // 添加触摸点跟踪，支持多点触摸
            static int touch_id = 1;  // 简单的触摸ID跟踪
            
            if (state == COMPOSITOR_INPUT_STATE_DOWN) {
                // 触摸开始，相当于鼠标按下
                log_message(3, "Touch down at (%d,%d), ID=%d", x, y, touch_id);
                compositor_handle_input(COMPOSITOR_INPUT_BUTTON, x, y, 1, COMPOSITOR_INPUT_STATE_DOWN);
                touch_id++;
            } else if (state == COMPOSITOR_INPUT_STATE_MOVE) {
                // 触摸移动，相当于鼠标移动
                compositor_handle_input(COMPOSITOR_INPUT_MOTION, x, y, 0, COMPOSITOR_INPUT_STATE_MOVE);
            } else if (state == COMPOSITOR_INPUT_STATE_UP) {
                // 触摸结束，相当于鼠标释放
                log_message(3, "Touch up at (%d,%d)", x, y);
                compositor_handle_input(COMPOSITOR_INPUT_BUTTON, x, y, 1, COMPOSITOR_INPUT_STATE_UP);
            }
            break;
    }
}

// 获取最后一个错误
int compositor_get_last_error() {
    return compositor_state.last_error;
}

// 获取错误消息
const char* compositor_get_error_message(char* buffer, size_t size) {
    if (buffer && size > 0) {
        strncpy(buffer, compositor_state.error_message, size - 1);
        buffer[size - 1] = '\0';
        return buffer;
    }
    return compositor_state.error_message;
}

// 获取最近的错误信息
const char* compositor_get_error(void) {
    return compositor_get_error_message(NULL, 0);
}

// 获取FPS
float compositor_get_fps() {
    return compositor_state.current_fps;
}

// 调整窗口大小
int compositor_resize(int width, int height) {
    if (!compositor_state.initialized) {
        set_error(COMPOSITOR_ERROR_INVALID_STATE, "Compositor not initialized");
        return -1;
    }
    
    log_message(2, "Resizing compositor to %dx%d", width, height);
    
    // 更新尺寸
    compositor_state.width = width;
    compositor_state.height = height;
    
    // 重新配置输出
    if (compositor_state.output) {
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_mode(&state, NULL);
        wlr_output_state_set_custom_mode(&state, width, height, compositor_state.config.preferred_refresh_rate * 1000);
        wlr_output_commit_state(compositor_state.output, &state);
        wlr_output_state_finish(&state);
    }
    
    // 重建交换链
    return recreate_swapchain() ? 0 : -1;
}

// 获取活动窗口标题
const char* compositor_get_active_window_title() {
    if (!compositor_state.initialized || !compositor_state.active_surface) {
        return NULL;
    }
    return compositor_state.active_surface->title ? compositor_state.active_surface->title : "(unnamed)";
}

// 定义窗口状态常量
enum WindowState {
    WINDOW_STATE_NORMAL = 0,
    WINDOW_STATE_MINIMIZED = 1,
    WINDOW_STATE_MAXIMIZED = 2
};

#define WINDOW_TITLEBAR_HEIGHT 30

// XWayland窗口状态数据结构
typedef struct {
    struct wlr_xwayland_surface *surface;
    struct {
        enum WindowState state; // 窗口状态
        int saved_x, saved_y; // 保存的窗口位置
        int saved_width, saved_height; // 保存的窗口大小
        bool is_fullscreen; // 是否全屏
        struct wl_list link;
    } state;
} XWaylandWindowState;

// Wayland窗口状态数据结构
typedef struct {
    WaylandWindow *window;
    struct {
        enum WindowState state; // 窗口状态
        int saved_x, saved_y; // 保存的窗口位置
        int saved_width, saved_height; // 保存的窗口大小
        bool is_fullscreen; // 是否全屏
        struct wl_list link;
    } state;
} WaylandWindowState;

// 保存窗口状态列表
static struct wl_list xwayland_window_states_list = WL_LIST_HEAD_INIT(xwayland_window_states_list);
static struct wl_list wayland_window_states_list = WL_LIST_HEAD_INIT(wayland_window_states_list);

// 查找或创建XWayland窗口状态
static XWaylandWindowState* get_xwayland_window_state(struct wlr_xwayland_surface *surface) {
    XWaylandWindowState *state;
    wl_list_for_each(state, &xwayland_window_states_list, state.link) {
        if (state->surface == surface) {
            return state;
        }
    }
    
    // 创建新的窗口状态
    state = calloc(1, sizeof(XWaylandWindowState));
    if (!state) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate XWayland window state");
        return NULL;
    }
    
    // 初始化链表节点
    wl_list_init(&state->state.link);
    
    state->surface = surface;
    state->state.state = WINDOW_STATE_NORMAL;
    state->state.is_fullscreen = false;
    
    // 只有在surface有效时才尝试获取尺寸
    if (surface) {
        state->state.saved_x = surface->x;
        state->state.saved_y = surface->y;
        state->state.saved_width = surface->surface ? surface->surface->current.width : 0;
        state->state.saved_height = surface->surface ? surface->surface->current.height : 0;
    }
    
    wl_list_insert(&xwayland_window_states_list, &state->state.link);
    log_message(3, "Created new XWayland window state for surface at %p", surface);
    return state;
}

// 移除XWayland窗口状态
static void remove_xwayland_window_state(struct wlr_xwayland_surface *surface) {
    XWaylandWindowState *state, *tmp;
    wl_list_for_each_safe(state, tmp, &xwayland_window_states_list, state.link) {
        if (state->surface == surface) {
            wl_list_remove(&state->state.link);
            free(state);
            log_message(3, "Removed XWayland window state for surface at %p", surface);
            break;
        }
    }
}

// 查找或创建Wayland窗口状态
static WaylandWindowState* get_wayland_window_state(WaylandWindow *window) {
    WaylandWindowState *state;
    wl_list_for_each(state, &wayland_window_states_list, state.link) {
        if (state->window == window) {
            return state;
        }
    }
    
    // 创建新的窗口状态
    state = calloc(1, sizeof(WaylandWindowState));
    if (!state) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate Wayland window state");
        return NULL;
    }
    
    // 初始化链表节点
    wl_list_init(&state->state.link);
    
    state->window = window;
    state->state.state = WINDOW_STATE_NORMAL;
    state->state.is_fullscreen = false;
    
    // 只有在window有效时才尝试获取尺寸
    if (window) {
        state->state.saved_x = window->x;
        state->state.saved_y = window->y;
        state->state.saved_width = window->width;
        state->state.saved_height = window->height;
    }
    
    wl_list_insert(&wayland_window_states_list, &state->state.link);
    log_message(3, "Created new Wayland window state for window at %p", window);
    return state;
}

// 移除Wayland窗口状态
static void remove_wayland_window_state(WaylandWindow *window) {
    WaylandWindowState *state, *tmp;
    wl_list_for_each_safe(state, tmp, &wayland_window_states_list, state.link) {
        if (state->window == window) {
            wl_list_remove(&state->state.link);
            free(state);
            log_message(3, "Removed Wayland window state for window at %p", window);
            break;
        }
    }
}

// 设置窗口焦点
int compositor_set_window_focus(const char* window_title) {
    if (!compositor_state.initialized || !window_title) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // 先尝试查找Wayland窗口
    WaylandWindow *window = find_wayland_window_by_title(window_title);
    if (window) {
        log_message(2, "Setting focus to Wayland window: %s", window_title);
        
        // 如果窗口被最小化，先还原它
        if (window->minimized) {
            log_message(3, "Wayland window is minimized, restoring it before focusing");
            window->minimized = false;
            
            // 如果窗口之前被移出屏幕，恢复到居中位置
            if (window->y > compositor_state.height) {
                window->y = compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0;
            }
            
            // 标记窗口区域为脏
            mark_dirty_rect(window->x, window->y, window->width, 
                          window->height + (compositor_state.config.enable_window_decoration ? 30 : 0));
        }
        
        // 将窗口移到链表前面（调整Z顺序）
        wl_list_remove(&window->link);
        wl_list_insert(&compositor_state.surfaces, &window->link);
        
        return 0;
    }
    
    // 查找Xwayland窗口
    struct wlr_xwayland_surface *surface = find_xwayland_surface_by_title(window_title);
    if (surface) {
        log_message(2, "Setting focus to Xwayland window: %s", window_title);
        
        // 获取窗口状态
        XWaylandWindowState *window_state = get_xwayland_window_state(surface);
        if (!window_state) {
            return -1;
        }
        
        // 如果窗口被最小化，先还原它
        if (window_state->state.state == WINDOW_STATE_MINIMIZED) {
            log_message(3, "Window is minimized, restoring it before focusing");
            
            // 还原到保存的状态
            wlr_xwayland_surface_configure(surface, 
                                          window_state->state.saved_x, 
                                          window_state->state.saved_y, 
                                          window_state->state.saved_width, 
                                          window_state->state.saved_height);
            
            window_state->state.state = WINDOW_STATE_NORMAL;
            
            // 标记窗口区域为脏
            mark_dirty_rect(window_state->state.saved_x, window_state->state.saved_y, 
                          window_state->state.saved_width, 
                          window_state->state.saved_height + (compositor_state.config.enable_window_decoration ? 30 : 0));
        }
        
        // 将窗口移到链表前面（调整Z顺序）
        wl_list_remove(&surface->link);
        wl_list_insert(&compositor_state.xwayland_surfaces, &surface->link);
        
        // 更新活动表面
        compositor_state.active_surface = surface;
        
        // 通知客户端窗口获得焦点
        wlr_xwayland_surface_activate(surface, true);
        
        return 0;
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 移动窗口到最前
int compositor_activate_window(const char* window_title) {
    if (!compositor_state.initialized || !window_title) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // 先尝试查找Wayland窗口
    WaylandWindow *window = find_wayland_window_by_title(window_title);
    if (window) {
        // 标记当前窗口位置为脏
        if (window->y < compositor_state.height) {
            mark_dirty_rect(window->x, window->y, window->width, 
                          window->height + (compositor_state.config.enable_window_decoration ? 30 : 0));
        }
        
        // 将窗口移到链表前面（调整Z顺序）
        wl_list_remove(&window->link);
        wl_list_insert(&compositor_state.surfaces, &window->link);
        
        // 如果窗口被最小化，先还原它
        if (window->minimized) {
            window->minimized = false;
            
            // 如果窗口之前被移出屏幕，恢复到居中位置
            if (window->y > compositor_state.height) {
                window->y = compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0;
            }
            
            // 标记还原后的窗口位置为脏
            mark_dirty_rect(window->x, window->y, window->width, 
                          window->height + (compositor_state.config.enable_window_decoration ? 30 : 0));
        }
        
        log_message(2, "Wayland window activated: %s", window_title);
        return 0;
    }
    
    // 查找Xwayland窗口
    struct wlr_xwayland_surface *surface = find_xwayland_surface_by_title(window_title);
    if (surface) {
        // 标记当前窗口位置为脏
        if (surface->y < compositor_state.height && surface->surface) {
            mark_dirty_rect(surface->x, surface->y, surface->surface->current.width, 
                          surface->surface->current.height + (compositor_state.config.enable_window_decoration ? 30 : 0));
        }
        
        // 将窗口移到链表前面（调整Z顺序）
        wl_list_remove(&surface->link);
        wl_list_insert(&compositor_state.xwayland_surfaces, &surface->link);
        
        // 更新活动表面
        compositor_state.active_surface = surface;
        
        log_message(2, "Xwayland window activated: %s", window_title);
        return 0;
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 关闭指定窗口
int compositor_close_window(const char* window_title) {
    if (!compositor_state.initialized || !window_title) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // 先尝试查找Wayland窗口
    WaylandWindow *window = find_wayland_window_by_title(window_title);
    if (window) {
        // 标记窗口区域为脏
        mark_dirty_rect(window->x, window->y, window->width, 
                      window->height + (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0));
        
        // 从链表中移除
        wl_list_remove(&window->link);
        
        // 释放窗口标题内存（如果是动态分配的）
        if (window->title) {
            free((void*)window->title);
        }
        
        free(window);
        
        log_message(2, "Closed Wayland window: %s", window_title);
        return 0;
    }
    
    // 查找Xwayland窗口
    struct wlr_xwayland_surface *surface = find_xwayland_surface_by_title(window_title);
    if (surface) {
        // 标记窗口区域为脏
        mark_dirty_rect(surface->x, surface->y, surface->surface ? surface->surface->current.width : 0, 
                      (surface->surface ? surface->surface->current.height : 0) + (compositor_state.config.enable_window_decoration ? 30 : 0));
        
        log_message(2, "Closing Xwayland window: %s", window_title);
        
        // 如果是当前活动窗口，清除活动窗口引用
        if (compositor_state.active_surface == surface) {
            compositor_state.active_surface = NULL;
            
            // 尝试将下一个窗口设为活动窗口
            if (!wl_list_empty(&compositor_state.xwayland_surfaces)) {
                struct wlr_xwayland_surface *next_surface = 
                    wl_container_of(compositor_state.xwayland_surfaces.next, next_surface, link);
                compositor_state.active_surface = next_surface;
            }
        }
        
        // 发送关闭事件到Xwayland表面
        wlr_xwayland_surface_close(surface);
        return 0;
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 调整窗口大小
int compositor_resize_window(const char* window_title, int width, int height) {
    if (!compositor_state.initialized || !window_title || width <= 0 || height <= 0) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // 设置最小尺寸限制
    const int min_width = 100;
    const int max_width = compositor_state.width;
    const int min_height = 60;
    const int max_height = compositor_state.height;
    
    // 限制窗口大小在合理范围内
    width = width < min_width ? min_width : (width > max_width ? max_width : width);
    height = height < min_height ? min_height : (height > max_height ? max_height : height);
    
    // 查找并调整Xwayland窗口大小
    struct wlr_xwayland_surface *surface;
    wl_list_for_each(surface, &compositor_state.xwayland_surfaces, link) {
        if (surface->title && strcmp(surface->title, window_title) == 0 && surface->surface) {
            log_message(2, "Resizing Xwayland window %s to %dx%d", window_title, width, height);
            
            // 保存原始位置用于脏区域标记
            int old_x = surface->x;
            int old_y = surface->y;
            int old_width = surface->surface->current.width;
            int old_height = surface->surface->current.height;
            
            // 标记旧窗口区域为脏
            mark_dirty_rect(old_x, old_y, old_width, 
                          old_height + (compositor_state.config.enable_window_decoration ? 30 : 0));
            
            // 确保窗口不超出屏幕边界
            if (old_x + width > compositor_state.width) {
                old_x = compositor_state.width - width;
            }
            if (old_y + height > compositor_state.height) {
                old_y = compositor_state.height - height;
            }
            
            // 最小化位置调整，确保窗口至少有一部分在屏幕内
            if (old_x < 0) old_x = 0;
            if (old_y < 0) old_y = 0;
            
            // 设置窗口大小和位置
            wlr_xwayland_surface_configure(surface, old_x, old_y, width, height);
            
            // 标记新窗口区域为脏
            mark_dirty_rect(old_x, old_y, width, 
                          height + (compositor_state.config.enable_window_decoration ? 30 : 0));
            
            return 0;
        }
    }
    
    // 查找并调整Wayland窗口大小
    WaylandWindow *wayland_window;
    wl_list_for_each(wayland_window, &compositor_state.surfaces, link) {
        if (wayland_window->title && strcmp(wayland_window->title, window_title) == 0 && wayland_window->mapped) {
            log_message(2, "Resizing Wayland window %s to %dx%d", window_title, width, height);
            
            // 保存原始位置
            int old_x = wayland_window->x;
            int old_y = wayland_window->y;
            
            // 确保窗口不超出屏幕边界
            if (old_x + width > compositor_state.width) {
                old_x = compositor_state.width - width;
            }
            if (old_y + height > compositor_state.height) {
                old_y = compositor_state.height - height;
            }
            
            // 最小化位置调整，确保窗口至少有一部分在屏幕内
            if (old_x < 0) old_x = 0;
            if (old_y < 0) old_y = 0;
            
            // 取消最大化状态，如果窗口当前是最大化的
            wayland_window->maximized = false;
            
            // 设置窗口大小和位置
            wayland_window->x = old_x;
            wayland_window->y = old_y;
            wayland_window->width = width;
            wayland_window->height = height;
            
            return 0;
        }
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 移动窗口位置
int compositor_move_window(const char* window_title, int x, int y) {
    if (!compositor_state.initialized || !window_title) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // 查找并移动Xwayland窗口
    struct wlr_xwayland_surface *surface;
    wl_list_for_each(surface, &compositor_state.xwayland_surfaces, link) {
        if (surface->title && strcmp(surface->title, window_title) == 0) {
            // 获取窗口大小以进行边界检查
            int window_width = surface->surface ? surface->surface->current.width : 0;
            int window_height = surface->surface ? surface->surface->current.height : 0;
            
            // 限制窗口位置，确保窗口至少有一部分可见
            if (x + window_width < 0) {
                x = -window_width / 2;  // 允许一半窗口在屏幕外
            } else if (x > compositor_state.width) {
                x = compositor_state.width - window_width / 2;
            }
            
            if (y + window_height < 0) {
                y = -window_height / 2;
            } else if (y > compositor_state.height) {
                y = compositor_state.height - window_height / 2;
            }
            
            log_message(2, "Moving Xwayland window %s to (%d,%d)", window_title, x, y);
            
            // 更新窗口位置
            // 标记旧位置和新位置为脏区域
            int old_x = surface->x;
            int old_y = surface->y;
            
            // 标记旧位置
            mark_dirty_rect(old_x, old_y, window_width, 
                          window_height + (compositor_state.config.enable_window_decoration ? 30 : 0));
            
            // 更新位置
            surface->x = x;
            surface->y = y;
            
            // 标记新位置
            mark_dirty_rect(x, y, window_width, 
                          window_height + (compositor_state.config.enable_window_decoration ? 30 : 0));
            
            // 如果移动的是活动窗口，保持其活动状态
            if (compositor_state.active_surface == surface) {
                // 可以在这里添加额外的焦点管理逻辑
            }
            
            return 0;
        }
    }
    
    // 查找并移动Wayland窗口
    WaylandWindow *wayland_window;
    wl_list_for_each(wayland_window, &compositor_state.surfaces, link) {
        if (wayland_window->title && strcmp(wayland_window->title, window_title) == 0) {
            // 限制窗口位置，确保窗口至少有一部分可见
            if (x + wayland_window->width < 0) {
                x = -wayland_window->width / 2;  // 允许一半窗口在屏幕外
            } else if (x > compositor_state.width) {
                x = compositor_state.width - wayland_window->width / 2;
            }
            
            if (y + wayland_window->height < 0) {
                y = -wayland_window->height / 2;
            } else if (y > compositor_state.height) {
                y = compositor_state.height - wayland_window->height / 2;
            }
            
            log_message(2, "Moving Wayland window %s to (%d,%d)", window_title, x, y);
            
            // 取消最大化状态，如果窗口当前是最大化的
            wayland_window->maximized = false;
            
            // 更新窗口位置
            wayland_window->x = x;
            wayland_window->y = y;
            
            return 0;
        }
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 获取窗口信息（位置和大小）
int compositor_get_window_info(const char* window_title, int* x, int* y, int* width, int* height) {
    if (!compositor_state.initialized || !window_title || !x || !y || !width || !height) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // 查找Xwayland窗口
    struct wlr_xwayland_surface *surface;
    wl_list_for_each(surface, &compositor_state.xwayland_surfaces, link) {
        if (surface->title && strcmp(surface->title, window_title) == 0) {
            *x = surface->x;
            *y = surface->y;
            *width = surface->surface ? surface->surface->current.width : 0;
            *height = surface->surface ? surface->surface->current.height : 0;
            log_message(3, "Retrieved info for Xwayland window %s: pos=(%d,%d), size=(%dx%d)", 
                       window_title, *x, *y, *width, *height);
            return 0;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindow *wayland_window;
    wl_list_for_each(wayland_window, &compositor_state.surfaces, link) {
        if (wayland_window->title && strcmp(wayland_window->title, window_title) == 0) {
            *x = wayland_window->x;
            *y = wayland_window->y;
            *width = wayland_window->width;
            *height = wayland_window->height;
            log_message(3, "Retrieved info for Wayland window %s: pos=(%d,%d), size=(%dx%d)", 
                       window_title, *x, *y, *width, *height);
            return 0;
        }
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 获取窗口列表
int compositor_get_window_list(char*** window_titles, int* count) {
    if (!compositor_state.initialized || !window_titles || !count) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // 计算窗口总数
    int total_count = 0;
    struct wlr_xwayland_surface *surface;
    wl_list_for_each(surface, &compositor_state.xwayland_surfaces, link) {
        if (surface->title) {
            total_count++;
        }
    }
    
    // 计算Wayland窗口数量
    WaylandWindow *wayland_window;
    wl_list_for_each(wayland_window, &compositor_state.surfaces, link) {
        if (wayland_window->title) {
            total_count++;
        }
    }
    
    if (total_count == 0) {
        *window_titles = NULL;
        *count = 0;
        return 0;
    }
    
    // 分配内存
    *window_titles = (char**)malloc(sizeof(char*) * total_count);
    if (!*window_titles) {
        set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for window list");
        return -1;
    }
    
    // 填充Xwayland窗口标题
    int index = 0;
    wl_list_for_each(surface, &compositor_state.xwayland_surfaces, link) {
        if (surface->title) {
            (*window_titles)[index] = strdup(surface->title);
            if (!(*window_titles)[index]) {
                // 清理已分配的内存
                compositor_free_window_list(window_titles, index);
                set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for window title");
                return -1;
            }
            index++;
        }
    }
    
    // 填充Wayland窗口标题
    wl_list_for_each(wayland_window, &compositor_state.surfaces, link) {
        if (wayland_window->title) {
            (*window_titles)[index] = strdup(wayland_window->title);
            if (!(*window_titles)[index]) {
                // 清理已分配的内存
                compositor_free_window_list(window_titles, index);
                set_error(COMPOSITOR_ERROR_MEMORY, "Failed to allocate memory for window title");
                return -1;
            }
            index++;
        }
    }
    
    *count = total_count;
    log_message(2, "Retrieved window list with %d entries", total_count);
    return 0;
}

// 释放窗口列表
void compositor_free_window_list(char*** window_titles, int count) {
    if (!window_titles || !*window_titles || count <= 0) {
        return;
    }
    
    for (int i = 0; i < count; i++) {
        if ((*window_titles)[i]) {
            free((*window_titles)[i]);
            (*window_titles)[i] = NULL;
        }
    }
    
    free(*window_titles);
    *window_titles = NULL;
    log_message(3, "Freed window list with %d entries", count);
}

// 统一的窗口操作执行函数
static bool execute_window_operation(WindowOps *ops, void *window, WindowOperation operation, const char *window_title) {
    WindowSavedState *state;
    struct wlr_box old_box = {0};
    
    // 获取当前窗口状态
    state = ops->get_state(window);
    if (!state) {
        log_message(1, "Failed to get window state for %s", window_title);
        return false;
    }
    
    // 标记旧区域为脏
    ops->get_dimensions(window, &old_box);
    mark_dirty_rect(&old_box);
    
    // 保存当前状态
    ops->save_state(window, state);
    
    // 执行操作
    if (!ops->apply_operation(window, operation)) {
        log_message(1, "Failed to apply %d operation to window %s", operation, window_title);
        return false;
    }
    
    // 标记新区域为脏
    struct wlr_box new_box = {0};
    ops->get_dimensions(window, &new_box);
    mark_dirty_rect(&new_box);
    
    return true;
}

// 最小化窗口
int compositor_minimize_window(const char* window_title) {
    return perform_window_operation(window_title, WINDOW_OP_MINIMIZE);
}

// 最小化窗口（按指针）
void compositor_minimize_window_by_ptr(void* window_ptr, bool is_wayland_window) {
    if (!window_ptr) {
        log_message(0, "Invalid window pointer in compositor_minimize_window_by_ptr");
        return;
    }
    
    if (is_wayland_window) {
        // 处理Wayland窗口
        WaylandWindow* window = window_ptr;
        WaylandWindowState* window_state = get_wayland_window_state(window);
        
        if (!window_state) {
            log_message(1, "Window state not found for Wayland window to minimize");
            return;
        }
        
        if (window_state->state.state == WINDOW_STATE_MINIMIZED) {
            log_message(2, "Wayland window is already minimized");
            return;
        }
        
        // 保存当前状态
        window_state->state.saved_x = window->x;
        window_state->state.saved_y = window->y;
        window_state->state.saved_width = window->width;
        window_state->state.saved_height = window->height;
        window_state->state.state = WINDOW_STATE_MINIMIZED;
        window_state->state.is_fullscreen = false;
        
        // 设置窗口为最小化状态
        window->minimized = true;
        
        // 标记窗口需要重新渲染
        mark_dirty_rect(window->x, window->y, window->width, window->height + (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0));
        
        // 最小化窗口 - 移出屏幕
        window->y = compositor_state.height + 100;
        
        log_message(2, "Minimized Wayland window: %s", window->title ? window->title : "(unnamed)");
    } else {
        // 处理Xwayland窗口
        struct wlr_xwayland_surface* xsurface = window_ptr;
        XwaylandWindowState* window_state = get_xwayland_window_state(xsurface);
        
        if (!window_state) {
            log_message(1, "Window state not found for Xwayland window to minimize");
            return;
        }
        
        if (window_state->state.state == WINDOW_STATE_MINIMIZED) {
            log_message(2, "Xwayland window is already minimized");
            return;
        }
        
        // 保存当前状态
        window_state->state.saved_x = xsurface->x;
        window_state->state.saved_y = xsurface->y;
        window_state->state.saved_width = xsurface->surface ? xsurface->surface->current.width : 0;
        window_state->state.saved_height = xsurface->surface ? xsurface->surface->current.height : 0;
        window_state->state.state = WINDOW_STATE_MINIMIZED;
        window_state->state.is_fullscreen = false;
        
        // 标记窗口需要重新渲染
        mark_dirty_rect(xsurface->x, xsurface->y, 
                        xsurface->surface ? xsurface->surface->current.width : 0, 
                        (xsurface->surface ? xsurface->surface->current.height : 0) + (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0));
        
        // 最小化窗口 - 移出屏幕
        wlr_xwayland_surface_configure(xsurface, xsurface->x, compositor_state.height + 100, 
                                      xsurface->surface ? xsurface->surface->current.width : 0, 
                                      xsurface->surface ? xsurface->surface->current.height : 0);
        
        log_message(2, "Minimized Xwayland window: %s", xsurface->title ? xsurface->title : "(unnamed)");
    }
}

// 窗口操作类型枚举
typedef enum {
    WINDOW_OP_MINIMIZE,
    WINDOW_OP_MAXIMIZE,
    WINDOW_OP_RESTORE
} WindowOperation;

// 窗口操作函数指针类型
typedef struct {
    void* (*get_window_state)(void* window);
    void (*save_window_state)(void* window_state, int x, int y, int width, int height);
    void (*apply_window_operation)(void* window, WindowOperation op, void* window_state);
    void (*mark_dirty)(int x, int y, int width, int height);
    int (*get_window_dimensions)(void* window, int* x, int* y, int* width, int* height);
} WindowOps;

// Xwayland窗口操作函数
static void* get_xwayland_window_state_wrapper(void* window) {
    return get_xwayland_window_state((struct wlr_xwayland_surface*)window);
}

static void save_xwayland_window_state(void* window_state, int x, int y, int width, int height) {
    XwaylandWindowState* state = (XwaylandWindowState*)window_state;
    state->state.saved_x = x;
    state->state.saved_y = y;
    state->state.saved_width = width;
    state->state.saved_height = height;
}

static void apply_xwayland_window_operation(void* window, WindowOperation op, void* window_state) {
    struct wlr_xwayland_surface* xsurface = (struct wlr_xwayland_surface*)window;
    XwaylandWindowState* state = (XwaylandWindowState*)window_state;
    
    switch (op) {
        case WINDOW_OP_MINIMIZE:
            state->state.state = WINDOW_STATE_MINIMIZED;
            wlr_xwayland_surface_configure(xsurface, xsurface->x, compositor_state.height + 100, 
                                         xsurface->surface ? xsurface->surface->current.width : 0, 
                                         xsurface->surface ? xsurface->surface->current.height : 0);
            break;
        case WINDOW_OP_MAXIMIZE:
            state->state.state = WINDOW_STATE_MAXIMIZED;
            wlr_xwayland_surface_configure(xsurface, 0, WINDOW_TITLEBAR_HEIGHT, 
                                         compositor_state.width, 
                                         compositor_state.height - WINDOW_TITLEBAR_HEIGHT);
            break;
        case WINDOW_OP_RESTORE:
            state->state.state = WINDOW_STATE_NORMAL;
            wlr_xwayland_surface_configure(xsurface, 
                                         state->state.saved_x, 
                                         state->state.saved_y, 
                                         state->state.saved_width, 
                                         state->state.saved_height);
            break;
    }
}

static void mark_xwayland_dirty(int x, int y, int width, int height) {
    mark_dirty_rect(x, y, width, height);
}

static int get_xwayland_window_dimensions(void* window, int* x, int* y, int* width, int* height) {
    struct wlr_xwayland_surface* xsurface = (struct wlr_xwayland_surface*)window;
    *x = xsurface->x;
    *y = xsurface->y;
    *width = xsurface->surface ? xsurface->surface->current.width : 0;
    *height = xsurface->surface ? xsurface->surface->current.height : 0;
    return 0;
}

// Wayland窗口操作函数
static void* get_wayland_window_state_wrapper(void* window) {
    return get_wayland_window_state((WaylandWindow*)window);
}

static void save_wayland_window_state(void* window_state, int x, int y, int width, int height) {
    WaylandWindowState* state = (WaylandWindowState*)window_state;
    state->state.saved_x = x;
    state->state.saved_y = y;
    state->state.saved_width = width;
    state->state.saved_height = height;
}

static void apply_wayland_window_operation(void* window, WindowOperation op, void* window_state) {
    WaylandWindow* wwindow = (WaylandWindow*)window;
    WaylandWindowState* state = (WaylandWindowState*)window_state;
    
    switch (op) {
        case WINDOW_OP_MINIMIZE:
            state->state.state = WINDOW_STATE_MINIMIZED;
            wwindow->minimized = true;
            wwindow->maximized = false;
            wwindow->y = compositor_state.height + 100;
            break;
        case WINDOW_OP_MAXIMIZE:
            state->state.state = WINDOW_STATE_MAXIMIZED;
            wwindow->maximized = true;
            wwindow->minimized = false;
            wwindow->x = 0;
            wwindow->y = compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0;
            wwindow->width = compositor_state.width;
            wwindow->height = compositor_state.height - (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0);
            break;
        case WINDOW_OP_RESTORE:
            state->state.state = WINDOW_STATE_NORMAL;
            wwindow->minimized = false;
            wwindow->maximized = false;
            wwindow->x = state->state.saved_x;
            wwindow->y = state->state.saved_y;
            wwindow->width = state->state.saved_width;
            wwindow->height = state->state.saved_height;
            break;
    }
}

static void mark_wayland_dirty(int x, int y, int width, int height) {
    mark_dirty_rect(x, y, width, height);
}

static int get_wayland_window_dimensions(void* window, int* x, int* y, int* width, int* height) {
    WaylandWindow* wwindow = (WaylandWindow*)window;
    *x = wwindow->x;
    *y = wwindow->y;
    *width = wwindow->width;
    *height = wwindow->height;
    return 0;
}

// 统一的窗口操作函数
static int perform_window_operation(const char* window_title, WindowOperation op) {
    if (!compositor_state.initialized || !window_title) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // Xwayland窗口操作
    struct wlr_xwayland_surface *xsurface;
    wl_list_for_each(xsurface, &compositor_state.xwayland_surfaces, link) {
        if (xsurface->title && strcmp(xsurface->title, window_title) == 0) {
            WindowOps xwayland_ops = {
                .get_window_state = get_xwayland_window_state_wrapper,
                .save_window_state = save_xwayland_window_state,
                .apply_window_operation = apply_xwayland_window_operation,
                .mark_dirty = mark_xwayland_dirty,
                .get_window_dimensions = get_xwayland_window_dimensions
            };
            
            XwaylandWindowState *window_state = (XwaylandWindowState*)xwayland_ops.get_window_state(xsurface);
            if (!window_state) {
                return -1;
            }
            
            // 获取当前窗口位置和尺寸
            int x, y, width, height;
            xwayland_ops.get_window_dimensions(xsurface, &x, &y, &width, &height);
            
            // 标记旧窗口区域为脏
            xwayland_ops.mark_dirty(x, y, width, height + (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0));
            
            // 保存当前状态（除非是还原操作且没有保存状态）
            if (op != WINDOW_OP_RESTORE || window_state->state.state != WINDOW_STATE_NORMAL) {
                xwayland_ops.save_window_state(window_state, x, y, width, height);
            }
            
            // 应用窗口操作
            xwayland_ops.apply_window_operation(xsurface, op, window_state);
            
            // 获取新窗口位置和尺寸
            xwayland_ops.get_window_dimensions(xsurface, &x, &y, &width, &height);
            
            // 标记新窗口区域为脏
            if (op == WINDOW_OP_MAXIMIZE) {
                mark_full_redraw(); // 最大化时全屏重绘
            } else {
                xwayland_ops.mark_dirty(x, y, width, height + (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0));
            }
            
            // 激活窗口
            if (compositor_state.active_surface != xsurface) {
                wl_list_remove(&xsurface->link);
                wl_list_insert(&compositor_state.xwayland_surfaces, &xsurface->link);
                compositor_state.active_surface = xsurface;
                wlr_xwayland_surface_activate(xsurface, true);
            }
            
            return 0;
        }
    }
    
    // Wayland窗口操作
    WaylandWindow *wayland_window;
    wl_list_for_each(wayland_window, &compositor_state.surfaces, link) {
        if (wayland_window->title && strcmp(wayland_window->title, window_title) == 0) {
            WindowOps wayland_ops = {
                .get_window_state = get_wayland_window_state_wrapper,
                .save_window_state = save_wayland_window_state,
                .apply_window_operation = apply_wayland_window_operation,
                .mark_dirty = mark_wayland_dirty,
                .get_window_dimensions = get_wayland_window_dimensions
            };
            
            WaylandWindowState *window_state = (WaylandWindowState*)wayland_ops.get_window_state(wayland_window);
            if (!window_state) {
                return -1;
            }
            
            // 获取当前窗口位置和尺寸
            int x, y, width, height;
            wayland_ops.get_window_dimensions(wayland_window, &x, &y, &width, &height);
            
            // 标记旧窗口区域为脏
            wayland_ops.mark_dirty(x, y, width, height + (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0));
            
            // 保存当前状态（除非是还原操作且没有保存状态）
            if (op != WINDOW_OP_RESTORE || window_state->state.state != WINDOW_STATE_NORMAL) {
                wayland_ops.save_window_state(window_state, x, y, width, height);
            }
            
            // 应用窗口操作
            wayland_ops.apply_window_operation(wayland_window, op, window_state);
            
            // 获取新窗口位置和尺寸
            wayland_ops.get_window_dimensions(wayland_window, &x, &y, &width, &height);
            
            // 标记新窗口区域为脏
            if (op == WINDOW_OP_MAXIMIZE) {
                mark_full_redraw(); // 最大化时全屏重绘
            } else {
                wayland_ops.mark_dirty(x, y, width, height + (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0));
            }
            
            // 将窗口移到链表前面（最上层）
            wl_list_remove(&wayland_window->link);
            wl_list_insert(&compositor_state.surfaces, &wayland_window->link);
            
            return 0;
        }
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 内部函数：根据窗口类型最大化窗口
void compositor_maximize_window_by_ptr(void* window_ptr, bool is_wayland_window) {
    if (!window_ptr) {
        log_message(0, "Invalid window pointer in compositor_maximize_window_by_ptr");
        return;
    }
    
    if (is_wayland_window) {
        // 处理Wayland窗口
        WaylandWindow* window = window_ptr;
        WaylandWindowState* window_state = get_wayland_window_state(window);
        
        if (!window_state) {
            log_message(1, "Window state not found for Wayland window to maximize");
            return;
        }
        
        if (window_state->state.state == WINDOW_STATE_MAXIMIZED) {
            log_message(2, "Wayland window is already maximized");
            return;
        }
        
        // 保存当前位置和大小
        window_state->state.saved_x = window->x;
        window_state->state.saved_y = window->y;
        window_state->state.saved_width = window->width;
        window_state->state.saved_height = window->height;
        
        // 设置窗口状态为最大化
        window_state->state.state = WINDOW_STATE_MAXIMIZED;
        window_state->state.is_fullscreen = false;
        
        // 获取输出尺寸
        int output_width = compositor_state.width;
        int output_height = compositor_state.height;
        
        // 应用新的位置和大小（最大化状态）
        window->x = 0;
        window->y = compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0;
        window->width = output_width;
        window->height = output_height - (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0);
        
        // 设置标志位
        window->maximized = true;
        window->minimized = false;
        
        // 标记窗口需要重新渲染
        mark_full_redraw();
        
        log_message(2, "Maximized Wayland window: %s", window->title ? window->title : "(unnamed)");
    } else {
        // 处理Xwayland窗口
        struct wlr_xwayland_surface* xsurface = window_ptr;
        XwaylandWindowState* window_state = get_xwayland_window_state(xsurface);
        
        if (!window_state) {
            log_message(1, "Window state not found for Xwayland window to maximize");
            return;
        }
        
        if (window_state->state.state == WINDOW_STATE_MAXIMIZED) {
            log_message(2, "Xwayland window is already maximized");
            return;
        }
        
        // 保存当前位置和大小
        window_state->state.saved_x = xsurface->x;
        window_state->state.saved_y = xsurface->y;
        window_state->state.saved_width = xsurface->surface ? xsurface->surface->current.width : 0;
        window_state->state.saved_height = xsurface->surface ? xsurface->surface->current.height : 0;
        
        // 设置窗口状态为最大化
        window_state->state.state = WINDOW_STATE_MAXIMIZED;
        window_state->state.is_fullscreen = false;
        
        // 获取输出尺寸
        int output_width = compositor_state.width;
        int output_height = compositor_state.height;
        
        // 应用新的位置和大小（最大化状态）
        wlr_xwayland_surface_configure(xsurface, 0, WINDOW_TITLEBAR_HEIGHT, output_width, 
                                      output_height - WINDOW_TITLEBAR_HEIGHT);
        
        // 标记窗口需要重新渲染
        mark_full_redraw();
        
        log_message(2, "Maximized Xwayland window: %s", xsurface->title ? xsurface->title : "(unnamed)");
    }
}

// 还原窗口
int compositor_restore_window(const char* window_title) {
    return perform_window_operation(window_title, WINDOW_OP_RESTORE);
}

// 设置窗口透明度
int compositor_set_window_opacity(const char* window_title, float opacity) {
    if (!compositor_state.initialized || !window_title) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // 限制透明度范围
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    
    log_message(2, "Setting window opacity for %s to %.2f", window_title, opacity);
    
    // 先尝试查找Wayland窗口
    WaylandWindow *window = find_wayland_window_by_title(window_title);
    if (window) {
        // 设置窗口透明度
        window->opacity = opacity;
        
        // 标记窗口区域为脏
        mark_dirty_rect(window->x, window->y, window->width, 
                      window->height + (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0));
        
        log_message(2, "Set opacity for Wayland window '%s' to %.2f", window_title, opacity);
        return 0;
    }
    
    // 查找Xwayland窗口
    struct wlr_xwayland_surface *surface = find_xwayland_surface_by_title(window_title);
    if (surface) {
        // 注意：Xwayland窗口透明度需要在渲染时处理
        set_error(COMPOSITOR_ERROR_UNSUPPORTED_OPERATION, "Window opacity not yet implemented for Xwayland surfaces");
        return -1;
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 最小化窗口
int compositor_minimize_window(const char* window_title) {
    return perform_window_operation(window_title, WINDOW_OP_MINIMIZE);
}
                        compositor_state.active_surface = NULL;
                    }
                }
                
                log_message(3, "Xwayland window %s minimized", window_title);
                return 0;
            }
        }
    }
    
    // 查找Wayland窗口
    WaylandWindow *wayland_window;
    wl_list_for_each(wayland_window, &compositor_state.surfaces, link) {
        if (wayland_window->title && strcmp(wayland_window->title, window_title) == 0) {
            // 获取窗口状态
            WaylandWindowState *window_state = get_wayland_window_state(wayland_window);
            if (window_state) {
                // 保存当前状态
                window_state->state.saved_x = wayland_window->x;
                window_state->state.saved_y = wayland_window->y;
                window_state->state.saved_width = wayland_window->width;
                window_state->state.saved_height = wayland_window->height;
                window_state->state.state = WINDOW_STATE_MINIMIZED;
            }
            
            // 标记当前窗口区域为脏
            mark_dirty_rect(wayland_window->x, wayland_window->y, wayland_window->width, 
                          wayland_window->height + (compositor_state.config.enable_window_decoration ? WINDOW_TITLEBAR_HEIGHT : 0));
            
            // 将窗口移出屏幕
            wayland_window->x = wayland_window->x;
            wayland_window->y = compositor_state.height + 100; // 移出屏幕
            
            // 设置窗口最小化标志
            wayland_window->minimized = true;
            
            // 如果是活动窗口，需要切换活动窗口
            if (compositor_state.active_wayland_window == wayland_window) {
                // 查找下一个窗口作为活动窗口
                WaylandWindow *next_window = NULL;
                wl_list_for_each(next_window, &compositor_state.surfaces, link) {
                    if (next_window != wayland_window) {
                        compositor_state.active_wayland_window = next_window;
                        break;
                    }
                }
                
                // 如果没有其他Wayland窗口，清除活动窗口
                if (!next_window) {
                    compositor_state.active_wayland_window = NULL;
                }
            }
            
            log_message(3, "Wayland window %s minimized", window_title);
            return 0;
        }
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 最大化窗口
int compositor_maximize_window(const char* window_title) {
    return perform_window_operation(window_title, WINDOW_OP_MAXIMIZE);
}

// 获取窗口Z顺序
int compositor_get_window_z_order(const char* window_title) {
    if (!compositor_state.initialized || !window_title) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    int z_order = 0;
    struct wlr_xwayland_surface *surface;
    
    // 按渲染顺序（链表顺序）计算Z顺序
    wl_list_for_each_reverse(surface, &compositor_state.xwayland_surfaces, link) {
        if (surface->title && strcmp(surface->title, window_title) == 0) {
            log_message(3, "Window %s Z order: %d", window_title, z_order);
            return z_order;
        }
        z_order++;
    }
    
    // 查找Wayland窗口Z顺序
    z_order = 0;
    WaylandWindow *wayland_window;
    
    // 按渲染顺序（链表顺序）计算Z顺序
    wl_list_for_each_reverse(wayland_window, &compositor_state.surfaces, link) {
        if (wayland_window->title && strcmp(wayland_window->title, window_title) == 0) {
            log_message(3, "Wayland window %s Z order: %d", window_title, z_order);
            return z_order;
        }
        z_order++;
    }
    
    set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
    return -1;
}

// 设置窗口Z顺序
int compositor_set_window_z_order(const char* window_title, int z_order) {
    if (!compositor_state.initialized || !window_title || z_order < 0) {
        set_error(COMPOSITOR_ERROR_INVALID_PARAMETER, "Invalid parameters");
        return -1;
    }
    
    // 查找窗口
    struct wlr_xwayland_surface *surface = NULL;
    struct wlr_xwayland_surface *tmp_surface;
    
    wl_list_for_each(tmp_surface, &compositor_state.xwayland_surfaces, link) {
        if (tmp_surface->title && strcmp(tmp_surface->title, window_title) == 0) {
            surface = tmp_surface;
            break;
        }
    }
    
    if (!surface) {
        set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window not found: %s", window_title);
        return -1;
    }
    
    // 限制Z顺序范围
    int total_surfaces = 0;
    struct wlr_xwayland_surface *count_surface;
    wl_list_for_each(count_surface, &compositor_state.xwayland_surfaces, link) {
        total_surfaces++;
    }
    
    if (z_order >= total_surfaces) {
        z_order = total_surfaces - 1;
    }
    
    // 先从链表中移除
    wl_list_remove(&surface->link);
    
    // 再插入到指定位置
    if (z_order == 0) {
        // 最顶层
        wl_list_insert(&compositor_state.xwayland_surfaces, &surface->link);
    } else {
        // 找到目标位置
        struct wlr_xwayland_surface *target = NULL;
        int index = 0;
        wl_list_for_each(target, &compositor_state.xwayland_surfaces, link) {
            if (index == z_order) {
                wl_list_insert(&target->link, &surface->link);
                break;
            }
            index++;
        }
        
        // 如果遍历结束还没找到目标位置，添加到链表末尾
        if (!target) {
            wl_list_insert(&compositor_state.xwayland_surfaces, &surface->link);
        }
    }
    
    log_message(2, "Window %s Z order set to %d", window_title, z_order);
    
    // 如果是设置为最顶层，更新活动窗口
    if (z_order == 0) {
        compositor_state.active_surface = surface;
    }
    
    return 0;
}