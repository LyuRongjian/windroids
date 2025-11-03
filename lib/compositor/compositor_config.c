#include "compositor_config.h"
#include "compositor_utils.h"
#include <stdlib.h>
#include <string.h>

// 默认配置
static CompositorConfig g_default_config = {
    // Xwayland 配置
    .enable_xwayland = 1,
    .xwayland_path = NULL,
    .xwayland_display_number = 0,
    
    // 渲染配置
    .enable_vsync = 1,
    .preferred_refresh_rate = 0,
    .max_swapchain_images = 3,
    .initial_scale = 1.0f,
    .render_quality = 100,
    .enable_animations = 1,
    .enable_alpha_compositing = 1,
    
    // 窗口管理
    .default_window_width = 800,
    .default_window_height = 600,
    .enable_window_decoration = 1,
    .max_windows = 10,
    
    // 调试选项
    .log_level = COMPOSITOR_LOG_INFO,
    .enable_tracing = 0,
    .enable_perf_monitoring = 0,
    .enable_debug_logging = 0,
    .debug_mode = 0,
    
    // 其他配置
    .background_color = {0.1f, 0.1f, 0.1f},
    .use_hardware_acceleration = 1,
    .refresh_rate = 0
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
        
        // 合并渲染配置
        merged->enable_vsync = user_config->enable_vsync;
        merged->preferred_refresh_rate = user_config->preferred_refresh_rate;
        merged->max_swapchain_images = user_config->max_swapchain_images;
        merged->initial_scale = user_config->initial_scale;
        merged->render_quality = user_config->render_quality;
        merged->enable_animations = user_config->enable_animations;
        merged->enable_alpha_compositing = user_config->enable_alpha_compositing;
        
        // 合并窗口管理
        merged->default_window_width = user_config->default_window_width;
        merged->default_window_height = user_config->default_window_height;
        merged->enable_window_decoration = user_config->enable_window_decoration;
        merged->max_windows = user_config->max_windows;
        
        // 合并调试选项
        merged->log_level = user_config->log_level;
        merged->enable_tracing = user_config->enable_tracing;
        merged->enable_perf_monitoring = user_config->enable_perf_monitoring;
        merged->enable_debug_logging = user_config->enable_debug_logging;
        merged->debug_mode = user_config->debug_mode;
        
        // 合并其他配置
        memcpy(merged->background_color, user_config->background_color, sizeof(float) * 3);
        merged->use_hardware_acceleration = user_config->use_hardware_acceleration;
        merged->refresh_rate = user_config->refresh_rate;
    }
    
    // 验证配置
    validate_config(merged);
    
    return merged;
}

// 验证配置
static void validate_config(CompositorConfig* config) {
    if (!config) return;
    
    // 验证窗口大小
    if (config->default_window_width < WINDOW_MIN_WIDTH) {
        config->default_window_width = WINDOW_MIN_WIDTH;
        log_message(COMPOSITOR_LOG_WARN, "Default window width too small, using minimum: %d", WINDOW_MIN_WIDTH);
    }
    
    if (config->default_window_height < WINDOW_MIN_HEIGHT) {
        config->default_window_height = WINDOW_MIN_HEIGHT;
        log_message(COMPOSITOR_LOG_WARN, "Default window height too small, using minimum: %d", WINDOW_MIN_HEIGHT);
    }
    
    // 验证渲染质量
    if (config->render_quality < 0) config->render_quality = 0;
    if (config->render_quality > 100) config->render_quality = 100;
    
    // 验证缩放比例
    if (config->initial_scale < 0.1f) config->initial_scale = 0.1f;
    if (config->initial_scale > 4.0f) config->initial_scale = 4.0f;
    
    // 验证最大窗口数量
    if (config->max_windows < 1) config->max_windows = 1;
    if (config->max_windows > 100) config->max_windows = 100;
    
    // 验证颜色值
    for (int i = 0; i < 3; i++) {
        if (config->background_color[i] < 0.0f) config->background_color[i] = 0.0f;
        if (config->background_color[i] > 1.0f) config->background_color[i] = 1.0f;
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
    
    // 渲染配置
    log_message(COMPOSITOR_LOG_INFO, "VSync: %s", config->enable_vsync ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Swapchain Images: %d", config->max_swapchain_images);
    log_message(COMPOSITOR_LOG_INFO, "Initial Scale: %.2f", config->initial_scale);
    log_message(COMPOSITOR_LOG_INFO, "Render Quality: %d%%", config->render_quality);
    log_message(COMPOSITOR_LOG_INFO, "Animations: %s", config->enable_animations ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Alpha Compositing: %s", config->enable_alpha_compositing ? "enabled" : "disabled");
    
    // 窗口管理
    log_message(COMPOSITOR_LOG_INFO, "Default Window Size: %dx%d", config->default_window_width, config->default_window_height);
    log_message(COMPOSITOR_LOG_INFO, "Window Decoration: %s", config->enable_window_decoration ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Max Windows: %d", config->max_windows);
    
    // 调试选项
    log_message(COMPOSITOR_LOG_INFO, "Log Level: %d", config->log_level);
    log_message(COMPOSITOR_LOG_INFO, "Tracing: %s", config->enable_tracing ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Performance Monitoring: %s", config->enable_perf_monitoring ? "enabled" : "disabled");
    log_message(COMPOSITOR_LOG_INFO, "Debug Mode: %s", config->debug_mode ? "enabled" : "disabled");
    
    // 其他配置
    log_message(COMPOSITOR_LOG_INFO, "Background Color: %.2f, %.2f, %.2f", 
               config->background_color[0], config->background_color[1], config->background_color[2]);
    log_message(COMPOSITOR_LOG_INFO, "Hardware Acceleration: %s", config->use_hardware_acceleration ? "enabled" : "disabled");
    if (config->refresh_rate > 0) {
        log_message(COMPOSITOR_LOG_INFO, "Refresh Rate: %d Hz", config->refresh_rate);
    }
    
    log_message(COMPOSITOR_LOG_INFO, "================================");
}