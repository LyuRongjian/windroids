/*
 * WinDroids Compositor
 * Main implementation file containing core functionality and module integration
 */

#include "compositor.h"
#include "compositor_config.h"
#include "compositor_window.h"
#include "compositor_input.h"
#include "compositor_vulkan.h"
#include "compositor_utils.h"
#include <android/native_window.h>
#include <stdlib.h>
#include <string.h>

// 全局状态
static CompositorState g_compositor_state;
static bool g_initialized = false;

// 初始化合成器
int compositor_init(ANativeWindow* window, int width, int height, CompositorConfig* config) {
    if (g_initialized) {
        log_message(COMPOSITOR_LOG_WARN, "Compositor already initialized");
        return COMPOSITOR_OK;
    }
    
    if (!window) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid window handle");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Initializing compositor...");
    
    // 初始化全局状态
    memset(&g_compositor_state, 0, sizeof(CompositorState));
    g_compositor_state.window = window;
    g_compositor_state.width = width;
    g_compositor_state.height = height;
    
    // 合并配置
    g_compositor_state.config = *compositor_merge_config(config);
    
    // 打印配置信息
    compositor_print_config(&g_compositor_state.config);
    
    // 设置日志级别
    utils_set_log_level(g_compositor_state.config.log_level);
    
    // 初始化各模块状态
    compositor_window_set_state(&g_compositor_state);
    compositor_input_set_state(&g_compositor_state);
    compositor_vulkan_set_state(&g_compositor_state);
    
    // 初始化Xwayland状态
    memset(&g_compositor_state.xwayland_state, 0, sizeof(XwaylandWindowState));
    g_compositor_state.xwayland_state.windows = NULL;
    g_compositor_state.xwayland_state.window_count = 0;
    g_compositor_state.xwayland_state.max_windows = g_compositor_state.config.max_windows;
    
    // 初始化Wayland状态
    memset(&g_compositor_state.wayland_state, 0, sizeof(WaylandWindowState));
    g_compositor_state.wayland_state.windows = NULL;
    g_compositor_state.wayland_state.window_count = 0;
    g_compositor_state.wayland_state.max_windows = g_compositor_state.config.max_windows;
    
    // 分配窗口数组
    if (g_compositor_state.config.enable_xwayland) {
        g_compositor_state.xwayland_state.windows = (XwaylandWindowState**)safe_malloc(
            sizeof(XwaylandWindowState*) * g_compositor_state.config.max_windows);
        if (!g_compositor_state.xwayland_state.windows) {
            goto cleanup;
        }
    }
    
    g_compositor_state.wayland_state.windows = (WaylandWindow**)safe_malloc(
        sizeof(WaylandWindow*) * g_compositor_state.config.max_windows);
    if (!g_compositor_state.wayland_state.windows) {
        goto cleanup;
    }
    
    // 初始化Vulkan
    if (g_compositor_state.config.use_hardware_acceleration) {
        if (init_vulkan(&g_compositor_state) != COMPOSITOR_OK) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize Vulkan");
            goto cleanup;
        }
    }
    
    // 初始化性能统计
    update_performance_stats();
    
    g_initialized = true;
    log_message(COMPOSITOR_LOG_INFO, "Compositor initialized successfully");
    return COMPOSITOR_OK;
    
cleanup:
    compositor_destroy();
    return COMPOSITOR_ERROR_INIT;
}

// 主循环单步
int compositor_step(void) {
    if (!g_initialized) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 更新性能统计
    update_performance_stats();
    
    // 检查是否需要重绘
    if (g_compositor_state.needs_redraw || g_compositor_state.config.debug_mode) {
        // 渲染帧
        if (g_compositor_state.config.use_hardware_acceleration) {
            if (render_frame() != COMPOSITOR_OK) {
                log_message(COMPOSITOR_LOG_ERROR, "Failed to render frame");
                return COMPOSITOR_ERROR_VULKAN;
            }
        } else {
            // 软件渲染路径（预留）
            log_message(COMPOSITOR_LOG_WARN, "Software rendering not implemented");
        }
        
        g_compositor_state.needs_redraw = false;
    }
    
    // 处理窗口事件
    process_window_events(&g_compositor_state);
    
    return COMPOSITOR_OK;
}

// 处理窗口事件
void process_window_events(CompositorState* state) {
    // 实际实现中会处理窗口创建、销毁等事件
    log_message(COMPOSITOR_LOG_DEBUG, "Processing window events");
}

// 销毁合成器
void compositor_destroy(void) {
    if (!g_initialized) return;
    
    log_message(COMPOSITOR_LOG_INFO, "Destroying compositor...");
    
    // 清理Vulkan资源
    if (g_compositor_state.config.use_hardware_acceleration) {
        cleanup_vulkan();
    }
    
    // 清理窗口
    cleanup_windows(&g_compositor_state);
    
    // 清理配置
    compositor_free_config(&g_compositor_state.config);
    
    // 清理各模块
    compositor_window_cleanup();
    compositor_input_cleanup();
    compositor_utils_cleanup();
    
    g_initialized = false;
    log_message(COMPOSITOR_LOG_INFO, "Compositor destroyed successfully");
}

// 清理窗口资源
void cleanup_windows(CompositorState* state) {
    // 清理Xwayland窗口
    if (state->xwayland_state.windows) {
        for (int i = 0; i < state->xwayland_state.window_count; i++) {
            if (state->xwayland_state.windows[i]) {
                if (state->xwayland_state.windows[i]->title) {
                    free((void*)state->xwayland_state.windows[i]->title);
                }
                free(state->xwayland_state.windows[i]);
            }
        }
        free(state->xwayland_state.windows);
    }
    
    // 清理Wayland窗口
    if (state->wayland_state.windows) {
        for (int i = 0; i < state->wayland_state.window_count; i++) {
            if (state->wayland_state.windows[i]) {
                if (state->wayland_state.windows[i]->title) {
                    free((void*)state->wayland_state.windows[i]->title);
                }
                free(state->wayland_state.windows[i]);
            }
        }
        free(state->wayland_state.windows);
    }
}

// 设置窗口大小
int compositor_resize(int width, int height) {
    if (!g_initialized) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (width <= 0 || height <= 0) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid window size");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Resizing compositor to %dx%d", width, height);
    
    g_compositor_state.width = width;
    g_compositor_state.height = height;
    
    // 重建交换链
    if (g_compositor_state.config.use_hardware_acceleration) {
        if (recreate_swapchain(width, height) != COMPOSITOR_OK) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to resize compositor");
            return COMPOSITOR_ERROR_SWAPCHAIN_ERROR;
        }
    }
    
    // 标记需要重绘
    g_compositor_state.needs_redraw = true;
    
    return COMPOSITOR_OK;
}

// 获取活动窗口标题
const char* compositor_get_active_window_title() {
    if (!g_initialized) {
        return NULL;
    }
    
    if (g_compositor_state.active_window) {
        if (g_compositor_state.active_window_is_wayland) {
            WaylandWindow* window = (WaylandWindow*)g_compositor_state.active_window;
            return window->title;
        } else {
            XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state.active_window;
            return window->title;
        }
    }
    
    return NULL;
}

// 触发重绘
void compositor_schedule_redraw(void) {
    if (g_initialized) {
        g_compositor_state.needs_redraw = true;
    }
}

// 查找位置处的表面
void* find_surface_at_position(int x, int y, bool* out_is_wayland) {
    if (!g_initialized || !out_is_wayland) {
        return NULL;
    }
    
    // 从顶层开始查找
    // 先查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state.wayland_state;
    for (int i = wayland_state->window_count - 1; i >= 0; i--) {
        WaylandWindow* window = wayland_state->windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x <= window->x + window->width + WINDOW_BORDER_WIDTH * 2 &&
            y >= window->y && y <= window->y + window->height + WINDOW_BORDER_WIDTH * 2 + WINDOW_TITLEBAR_HEIGHT) {
            *out_is_wayland = true;
            return window;
        }
    }
    
    // 再查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state.xwayland_state;
    for (int i = xwayland_state->window_count - 1; i >= 0; i--) {
        XwaylandWindowState* window = xwayland_state->windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x <= window->x + window->width + WINDOW_BORDER_WIDTH * 2 &&
            y >= window->y && y <= window->y + window->height + WINDOW_BORDER_WIDTH * 2 + WINDOW_TITLEBAR_HEIGHT) {
            *out_is_wayland = false;
            return window;
        }
    }
    
    *out_is_wayland = false;
    return NULL;
}

// 获取当前状态（仅供内部使用）
CompositorState* compositor_get_state(void) {
    return g_initialized ? &g_compositor_state : NULL;
}

// 检查是否已初始化
bool compositor_is_initialized(void) {
    return g_initialized;
}
