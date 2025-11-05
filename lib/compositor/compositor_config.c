#include "compositor_config.h"
#include "compositor_utils.h"
#include <stdlib.h>
#include <string.h>

// 默认配置
static CompositorConfig g_default_config = {
    // Xwayland 配置
    .enable_xwayland = true,
    .xwayland_path = NULL,
    .xwayland_display_number = 0,
    .xwayland_force_fullscreen = false,
    
    // 渲染配置
    .enable_vsync = true,
    .preferred_refresh_rate = 0,
    .max_swapchain_images = 3,
    .initial_scale = 1.0f,
    .render_quality = 100,
    .enable_animations = true,
    .enable_alpha_compositing = true,
    .enable_dirty_rects = true,
    .max_dirty_rects = 100,
    .enable_scissor_test = true,
    
    // 窗口管理
    .window_manager = {
        .default_window_width = 800,
        .default_window_height = 600,
        .enable_window_decoration = true,
        .max_windows = 20,
        .enable_window_cycling = true,
        .window_border_width = 2,
        .window_titlebar_height = 30,
        .enable_window_shadows = true,
        .window_shadow_opacity = 0.5f,
        .enable_hover_effects = true,
        .enable_window_rotation = true,
        .wraparound_workspaces = true
    },
    
    // 内存管理
    .texture_cache_size_mb = 256,
    .texture_cache_max_items = 1000,
    .enable_memory_tracking = false,
    .max_memory_usage_mb = 512,
    .enable_memory_compression = false,
    
    // 输入设备
    .enable_mouse = true,
    .enable_keyboard = true,
    .enable_touch = true,
    .enable_gestures = true,
    .enable_gamepad = false,
    .enable_pen = false,
    .enable_trackball = false,
    .enable_touchpad = true,
    .max_touch_points = 10,
    .pen_pressure_sensitivity = 0.5f,
    .joystick_sensitivity = 1.0f,
    .joystick_mouse_emulation = false,
    .enable_pen_pressure = true,
    .enable_gesture_window_manipulation = true,
    .enable_edge_snap = true,
    .edge_snap_threshold = 10,
    .enable_workspace_edge_switch = true,
    .workspace_switch_delay = 500,
    
    // 性能优化
    .enable_multithreading = true,
    .render_thread_count = 2,
    .enable_swap_interval_adaptation = true,
    .enable_async_texture_upload = true,
    .enable_batch_rendering = true,
    
    // 调试选项
    .log_level = COMPOSITOR_LOG_INFO,
    .enable_tracing = false,
    .enable_perf_monitoring = false,
    .enable_debug_logging = false,
    .debug_mode = false,
    .show_fps_counter = false,
    
    // 其他配置
    .background_color = {0.1f, 0.1f, 0.1f},
    .use_hardware_acceleration = true,
    .refresh_rate = 0,
    .enable_screensaver = false,
    .screensaver_timeout = 300
};

// 获取默认配置
CompositorConfig* compositor_get_default_config(void) {
    // 返回默认配置的副本
    CompositorConfig* config = (CompositorConfig*)malloc(sizeof(CompositorConfig));
    if (!config) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for default config");
        return NULL;
    }
    
    memcpy(config, &g_default_config, sizeof(CompositorConfig));
    
    // 复制字符串
    if (g_default_config.xwayland_path) {
        config->xwayland_path = strdup(g_default_config.xwayland_path);
    }
    
    // 启用内存跟踪
    config->enable_memory_tracking = true;
    
    // 默认启用触摸和手势
    config->enable_touch = true;
    config->enable_gestures = true;
    config->enable_touchpad = true;
    config->enable_gesture_window_manipulation = true;
    config->enable_edge_snap = true;
    
    // 增加渲染线程数
    config->render_thread_count = 4;
    
    // 默认启用异步纹理上传
    config->enable_async_texture_upload = true;
    
    // 启用批处理渲染
    config->enable_batch_rendering = true;
    
    // 启用纹理压缩
    config->enable_memory_compression = true;
    
    return config;
}

// 设置日志级别
void compositor_set_log_level(int level) {
    if (level >= COMPOSITOR_LOG_ERROR && level <= COMPOSITOR_LOG_DEBUG) {
        g_default_config.log_level = level;
        log_message(COMPOSITOR_LOG_INFO, "Log level set to %d", level);
    } else {
        log_message(COMPOSITOR_LOG_WARN, "Invalid log level: %d, using default", level);
    }
}

// 合并配置（将用户配置合并到默认配置）
CompositorConfig* compositor_merge_config(CompositorConfig* user_config) {
    CompositorConfig* merged = compositor_get_default_config();
    if (!merged) {
        return NULL;
    }
    
    if (user_config) {
        // 合并Xwayland配置
        if (user_config->xwayland_path) {
            free((void*)merged->xwayland_path);
            merged->xwayland_path = strdup(user_config->xwayland_path);
        }
        merged->enable_xwayland = user_config->enable_xwayland;
        merged->xwayland_display_number = user_config->xwayland_display_number;
        merged->xwayland_force_fullscreen = user_config->xwayland_force_fullscreen;
        
        // 合并渲染配置
        merged->enable_vsync = user_config->enable_vsync;
        merged->preferred_refresh_rate = user_config->preferred_refresh_rate;
        merged->max_swapchain_images = user_config->max_swapchain_images;
        merged->initial_scale = user_config->initial_scale;
        merged->render_quality = user_config->render_quality;
        merged->enable_animations = user_config->enable_animations;
        merged->enable_alpha_compositing = user_config->enable_alpha_compositing;
        merged->enable_dirty_rects = user_config->enable_dirty_rects;
        merged->max_dirty_rects = user_config->max_dirty_rects;
        merged->enable_scissor_test = user_config->enable_scissor_test;
        
        // 合并窗口管理
        merged->window_manager.default_window_width = user_config->window_manager.default_window_width;
        merged->window_manager.default_window_height = user_config->window_manager.default_window_height;
        merged->window_manager.enable_window_decoration = user_config->window_manager.enable_window_decoration;
        merged->window_manager.max_windows = user_config->window_manager.max_windows;
        merged->window_manager.enable_window_cycling = user_config->window_manager.enable_window_cycling;
        merged->window_manager.window_border_width = user_config->window_manager.window_border_width;
        merged->window_manager.window_titlebar_height = user_config->window_manager.window_titlebar_height;
        merged->window_manager.enable_window_shadows = user_config->window_manager.enable_window_shadows;
        merged->window_manager.window_shadow_opacity = user_config->window_manager.window_shadow_opacity;
        merged->window_manager.enable_hover_effects = user_config->window_manager.enable_hover_effects;
        merged->window_manager.enable_window_rotation = user_config->window_manager.enable_window_rotation;
        merged->window_manager.wraparound_workspaces = user_config->window_manager.wraparound_workspaces;
        
        // 合并内存管理
        merged->texture_cache_size_mb = user_config->texture_cache_size_mb;
        merged->texture_cache_max_items = user_config->texture_cache_max_items;
        merged->enable_memory_tracking = user_config->enable_memory_tracking;
        merged->max_memory_usage_mb = user_config->max_memory_usage_mb;
        merged->enable_memory_compression = user_config->enable_memory_compression;
        
        // 合并输入设备
        merged->enable_mouse = user_config->enable_mouse;
        merged->enable_keyboard = user_config->enable_keyboard;
        merged->enable_touch = user_config->enable_touch;
        merged->enable_gestures = user_config->enable_gestures;
        merged->enable_gamepad = user_config->enable_gamepad;
        merged->enable_pen = user_config->enable_pen;
        merged->enable_trackball = user_config->enable_trackball;
        merged->enable_touchpad = user_config->enable_touchpad;
        merged->max_touch_points = user_config->max_touch_points;
        merged->pen_pressure_sensitivity = user_config->pen_pressure_sensitivity;
        merged->joystick_sensitivity = user_config->joystick_sensitivity;
        merged->joystick_mouse_emulation = user_config->joystick_mouse_emulation;
        merged->enable_pen_pressure = user_config->enable_pen_pressure;
        merged->enable_gesture_window_manipulation = user_config->enable_gesture_window_manipulation;
        merged->enable_edge_snap = user_config->enable_edge_snap;
        merged->edge_snap_threshold = user_config->edge_snap_threshold;
        merged->enable_workspace_edge_switch = user_config->enable_workspace_edge_switch;
        merged->workspace_switch_delay = user_config->workspace_switch_delay;
        
        // 合并性能优化
        merged->enable_multithreading = user_config->enable_multithreading;
        merged->render_thread_count = user_config->render_thread_count;
        merged->enable_swap_interval_adaptation = user_config->enable_swap_interval_adaptation;
        merged->enable_async_texture_upload = user_config->enable_async_texture_upload;
        merged->enable_batch_rendering = user_config->enable_batch_rendering;
        
        // 合并调试选项
        merged->log_level = user_config->log_level;
        merged->enable_tracing = user_config->enable_tracing;
        merged->enable_perf_monitoring = user_config->enable_perf_monitoring;
        merged->enable_debug_logging = user_config->enable_debug_logging;
        merged->debug_mode = user_config->debug_mode;
        merged->show_fps_counter = user_config->show_fps_counter;
        
        // 合并其他配置
        memcpy(merged->background_color, user_config->background_color, sizeof(float) * 3);
        merged->use_hardware_acceleration = user_config->use_hardware_acceleration;
        merged->refresh_rate = user_config->refresh_rate;
        merged->enable_screensaver = user_config->enable_screensaver;
        merged->screensaver_timeout = user_config->screensaver_timeout;
    }
    
    // 验证配置
    validate_config(merged);
    
    return merged;
}

// 验证配置
static void validate_config(CompositorConfig* config) {
    if (!config) return;
    
    // 验证窗口大小
    if (config->window_manager.default_window_width < WINDOW_MIN_WIDTH) {
        config->window_manager.default_window_width = WINDOW_MIN_WIDTH;
        log_message(COMPOSITOR_LOG_WARN, "Default window width too small, using minimum: %d", WINDOW_MIN_WIDTH);
    }
    
    if (config->window_manager.default_window_height < WINDOW_MIN_HEIGHT) {
        config->window_manager.default_window_height = WINDOW_MIN_HEIGHT;
        log_message(COMPOSITOR_LOG_WARN, "Default window height too small, using minimum: %d", WINDOW_MIN_HEIGHT);
    }
    
    // 验证渲染质量
    if (config->render_quality < 0) config->render_quality = 0;
    if (config->render_quality > 100) config->render_quality = 100;
    
    // 验证缩放比例
    if (config->initial_scale < 0.1f) config->initial_scale = 0.1f;
    if (config->initial_scale > 4.0f) config->initial_scale = 4.0f;
    
    // 验证最大窗口数量
    if (config->window_manager.max_windows < 1) config->window_manager.max_windows = 1;
    if (config->window_manager.max_windows > 100) config->window_manager.max_windows = 100;
    
    // 验证交换链图像数量
    if (config->max_swapchain_images < 2) config->max_swapchain_images = 2;
    if (config->max_swapchain_images > 8) config->max_swapchain_images = 8;
    
    // 验证日志级别
    if (config->log_level < COMPOSITOR_LOG_ERROR || config->log_level > COMPOSITOR_LOG_DEBUG) {
        config->log_level = COMPOSITOR_LOG_INFO;
    }
    
    // 验证颜色值
    for (int i = 0; i < 3; i++) {
        if (config->background_color[i] < 0.0f) config->background_color[i] = 0.0f;
        if (config->background_color[i] > 1.0f) config->background_color[i] = 1.0f;
    }
    
    // 验证最大脏区域数量
    if (config->max_dirty_rects < 1) config->max_dirty_rects = 1;
    if (config->max_dirty_rects > 1000) config->max_dirty_rects = 1000;
    
    // 验证窗口边框和标题栏尺寸
    if (config->window_manager.window_border_width < 0) config->window_manager.window_border_width = 0;
    if (config->window_manager.window_border_width > 20) config->window_manager.window_border_width = 20;
    if (config->window_manager.window_titlebar_height < 0) config->window_manager.window_titlebar_height = 0;
    if (config->window_manager.window_titlebar_height > 100) config->window_manager.window_titlebar_height = 100;
    
    // 验证阴影透明度
    if (config->window_manager.window_shadow_opacity < 0.0f) config->window_manager.window_shadow_opacity = 0.0f;
    if (config->window_manager.window_shadow_opacity > 1.0f) config->window_manager.window_shadow_opacity = 1.0f;
    
    // 验证内存管理配置
    if (config->texture_cache_size_mb < 16) config->texture_cache_size_mb = 16;
    if (config->texture_cache_size_mb > 2048) config->texture_cache_size_mb = 2048;
    if (config->texture_cache_max_items < 10) config->texture_cache_max_items = 10;
    if (config->texture_cache_max_items > 10000) config->texture_cache_max_items = 10000;
    if (config->max_memory_usage_mb < 64) config->max_memory_usage_mb = 64;
    if (config->max_memory_usage_mb > 8192) config->max_memory_usage_mb = 8192;
    
    // 验证触摸点数量
    if (config->max_touch_points < 1) config->max_touch_points = 1;
    if (config->max_touch_points > 32) config->max_touch_points = 32;
    
    // 验证输入设备灵敏度
    if (config->pen_pressure_sensitivity < 0.0f) config->pen_pressure_sensitivity = 0.0f;
    if (config->pen_pressure_sensitivity > 1.0f) config->pen_pressure_sensitivity = 1.0f;
    if (config->joystick_sensitivity < 0.1f) config->joystick_sensitivity = 0.1f;
    if (config->joystick_sensitivity > 10.0f) config->joystick_sensitivity = 10.0f;
    
    // 验证边缘吸附阈值
    if (config->edge_snap_threshold < 0) config->edge_snap_threshold = 0;
    if (config->edge_snap_threshold > 50) config->edge_snap_threshold = 50;
    
    // 验证工作区切换延迟
    if (config->workspace_switch_delay < 100) config->workspace_switch_delay = 100;
    if (config->workspace_switch_delay > 2000) config->workspace_switch_delay = 2000;
    
    // 验证线程配置
    if (config->render_thread_count < 1) config->render_thread_count = 1;
    if (config->render_thread_count > 16) config->render_thread_count = 16;
    
    // 验证屏幕保护超时
    if (config->screensaver_timeout < 60) config->screensaver_timeout = 60;
    if (config->screensaver_timeout > 3600) config->screensaver_timeout = 3600;
    
    // 强制使用硬件加速（根据需求）
    config->use_hardware_acceleration = true;
    
    // 如果启用了性能监控，同时启用调试日志
    if (config->enable_perf_monitoring) {
        config->enable_debug_logging = true;
    }
}

// 释放配置
void compositor_free_config(CompositorConfig* config) {
    if (config) {
        if (config->xwayland_path) {
            free((void*)config->xwayland_path);
        }
        free(config);
    }
}

// 打印配置信息
void compositor_print_config(const CompositorConfig* config) {
    if (!config) {
        log_message(COMPOSITOR_LOG_ERROR, "Cannot print NULL config");
        return;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "=== Compositor Configuration ===");
    
    // Xwayland 配置
    log_message(COMPOSITOR_LOG_INFO, "Xwayland: %s", config->enable_xwayland ? "enabled" : "disabled");
    if (config->xwayland_path) {
        log_message(COMPOSITOR_LOG_INFO, "Xwayland Path: %s", config->xwayland_path);
    }
    log_message(COMPOSITOR_LOG_INFO, "Xwayland Display: %d", config->xwayland_display_number);
    log_message(COMPOSITOR_LOG_INFO, "Xwayland Force Fullscreen: %s", config->xwayland_force_fullscreen ? "enabled" : "disabled");
    
    // 渲染配置
    log_message(COMPOSITOR_LOG_INFO, "VSync: %s", config->enable_vsync ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Swapchain Images: %d", config->max_swapchain_images);
    log_message(COMPOSITOR_LOG_INFO, "Initial Scale: %.2f", config->initial_scale);
    log_message(COMPOSITOR_LOG_INFO, "Render Quality: %d%%", config->render_quality);
    log_message(COMPOSITOR_LOG_INFO, "Animations: %s", config->enable_animations ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Alpha Compositing: %s", config->enable_alpha_compositing ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Dirty Rects: %s", config->enable_dirty_rects ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Max Dirty Rects: %d", config->max_dirty_rects);
    log_message(COMPOSITOR_LOG_INFO, "Scissor Test: %s", config->enable_scissor_test ? "enabled" : "disabled");
    
    // 窗口管理
    log_message(COMPOSITOR_LOG_INFO, "Default Window Size: %dx%d", config->default_window_width, config->default_window_height);
    log_message(COMPOSITOR_LOG_INFO, "Window Decoration: %s", config->enable_window_decoration ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Max Windows: %d", config->max_windows);
    log_message(COMPOSITOR_LOG_INFO, "Window Cycling: %s", config->enable_window_cycling ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Window Border Width: %d", config->window_border_width);
    log_message(COMPOSITOR_LOG_INFO, "Window Titlebar Height: %d", config->window_titlebar_height);
    log_message(COMPOSITOR_LOG_INFO, "Window Shadows: %s", config->enable_window_shadows ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Window Shadow Opacity: %.2f", config->window_shadow_opacity);
    
    // 内存管理
    log_message(COMPOSITOR_LOG_INFO, "Texture Cache Size: %zu MB", config->texture_cache_size_mb);
    log_message(COMPOSITOR_LOG_INFO, "Texture Cache Max Items: %d", config->texture_cache_max_items);
    log_message(COMPOSITOR_LOG_INFO, "Memory Tracking: %s", config->enable_memory_tracking ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Max Memory Usage: %zu MB", config->max_memory_usage_mb);
    log_message(COMPOSITOR_LOG_INFO, "Memory Compression: %s", config->enable_memory_compression ? "enabled" : "disabled");
    
    // 输入设备
    log_message(COMPOSITOR_LOG_INFO, "Mouse: %s", config->enable_mouse ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Keyboard: %s", config->enable_keyboard ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Touch: %s", config->enable_touch ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Gestures: %s", config->enable_gestures ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Gamepad: %s", config->enable_gamepad ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Pen: %s", config->enable_pen ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Max Touch Points: %d", config->max_touch_points);
    
    // 性能优化
    log_message(COMPOSITOR_LOG_INFO, "Multithreading: %s", config->enable_multithreading ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Render Thread Count: %d", config->render_thread_count);
    log_message(COMPOSITOR_LOG_INFO, "Swap Interval Adaptation: %s", config->enable_swap_interval_adaptation ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Async Texture Upload: %s", config->enable_async_texture_upload ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Batch Rendering: %s", config->enable_batch_rendering ? "enabled" : "disabled");
    
    // 调试选项
    log_message(COMPOSITOR_LOG_INFO, "Log Level: %d", config->log_level);
    log_message(COMPOSITOR_LOG_INFO, "Tracing: %s", config->enable_tracing ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Performance Monitoring: %s", config->enable_perf_monitoring ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Debug Logging: %s", config->enable_debug_logging ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Debug Mode: %s", config->debug_mode ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "FPS Counter: %s", config->show_fps_counter ? "enabled" : "disabled");
    
    // 其他配置
    log_message(COMPOSITOR_LOG_INFO, "Background Color: %.2f, %.2f, %.2f", 
               config->background_color[0], config->background_color[1], config->background_color[2]);
    log_message(COMPOSITOR_LOG_INFO, "Hardware Acceleration: %s", config->use_hardware_acceleration ? "enabled" : "disabled");
    if (config->refresh_rate > 0) {
        log_message(COMPOSITOR_LOG_INFO, "Refresh Rate: %d Hz", config->refresh_rate);
    }
    log_message(COMPOSITOR_LOG_INFO, "Screensaver: %s", config->enable_screensaver ? "enabled" : "disabled");
    if (config->enable_screensaver) {
        log_message(COMPOSITOR_LOG_INFO, "Screensaver Timeout: %d seconds", config->screensaver_timeout);
    }
    
    log_message(COMPOSITOR_LOG_INFO, "================================");
}