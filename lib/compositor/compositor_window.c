#include "compositor_window.h"
#include "compositor_utils.h"
#include "compositor.h"
#include <string.h>
#include <stdlib.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 安全内存分配函数
static void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory: %zu bytes", size);
    }
    return ptr;
}

// 安全内存重分配函数
static void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to reallocate memory: %zu bytes", size);
    }
    return new_ptr;
}

// 设置全局状态指针（由compositor.c调用）
void compositor_window_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 初始化窗口状态
void window_state_init(WindowSavedState* state) {
    if (!state) return;
    memset(state, 0, sizeof(WindowSavedState));
    state->state = WINDOW_STATE_NORMAL;
    state->saved_opacity = WINDOW_DEFAULT_OPACITY;
}

// 保存窗口状态
void window_save_state(WindowInfo* window, WindowSavedState* state) {
    if (!window || !state) return;
    
    state->state = window->state;
    state->saved_x = window->x;
    state->saved_y = window->y;
    state->saved_width = window->width;
    state->saved_height = window->height;
    state->is_fullscreen = (window->state == WINDOW_STATE_FULLSCREEN);
    state->saved_opacity = window->opacity;
}

// 恢复窗口状态
void window_restore_state(WindowInfo* window, WindowSavedState* state) {
    if (!window || !state) return;
    
    window->state = state->state;
    window->x = state->saved_x;
    window->y = state->saved_y;
    window->width = state->saved_width;
    window->height = state->saved_height;
    window->opacity = state->saved_opacity;
}

// 移动窗口到最前
int compositor_activate_window(const char* window_title) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for activate_window");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找并激活Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            xwayland_window_activate(xwayland_state->windows[i]);
            return COMPOSITOR_OK;
        }
    }
    
    // 查找并激活Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            wayland_window_activate(wayland_state->windows[i]);
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 关闭指定窗口
int compositor_close_window(const char* window_title) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for close_window");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找并关闭Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            xwayland_window_close(xwayland_state->windows[i]);
            return COMPOSITOR_OK;
        }
    }
    
    // 查找并关闭Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            wayland_window_close(wayland_state->windows[i]);
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 调整窗口大小
int compositor_resize_window(const char* window_title, int width, int height) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for resize_window");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 边界检查
    if (width < WINDOW_MIN_WIDTH) width = WINDOW_MIN_WIDTH;
    if (height < WINDOW_MIN_HEIGHT) height = WINDOW_MIN_HEIGHT;
    if (width > g_compositor_state->width) width = g_compositor_state->width;
    if (height > g_compositor_state->height) height = g_compositor_state->height;
    
    // 查找并调整Xwayland窗口大小
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            // 保存旧区域用于脏标记
            int old_x = xwayland_state->windows[i]->x;
            int old_y = xwayland_state->windows[i]->y;
            int old_width = xwayland_state->windows[i]->width;
            int old_height = xwayland_state->windows[i]->height;
            
            // 调整窗口大小
            xwayland_state->windows[i]->width = width;
            xwayland_state->windows[i]->height = height;
            xwayland_state->windows[i]->is_dirty = true;
            
            // 标记旧区域和新区域都需要重绘
            mark_dirty_rect(g_compositor_state, old_x, old_y, old_width, old_height);
            mark_dirty_rect(g_compositor_state, xwayland_state->windows[i]->x, 
                           xwayland_state->windows[i]->y, width, height);
            
            return COMPOSITOR_OK;
        }
    }
    
    // 查找并调整Wayland窗口大小
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            // 保存旧区域用于脏标记
            int old_x = wayland_state->windows[i]->x;
            int old_y = wayland_state->windows[i]->y;
            int old_width = wayland_state->windows[i]->width;
            int old_height = wayland_state->windows[i]->height;
            
            // 调整窗口大小
            wayland_state->windows[i]->width = width;
            wayland_state->windows[i]->height = height;
            wayland_state->windows[i]->is_dirty = true;
            
            // 标记旧区域和新区域都需要重绘
            mark_dirty_rect(g_compositor_state, old_x, old_y, old_width, old_height);
            mark_dirty_rect(g_compositor_state, wayland_state->windows[i]->x, 
                           wayland_state->windows[i]->y, width, height);
            
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 移动窗口位置
int compositor_move_window(const char* window_title, int x, int y) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for move_window");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            // 边界检查
            int new_x = x;
            int new_y = y;
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x > g_compositor_state->width - xwayland_state->windows[i]->width - WINDOW_BORDER_WIDTH * 2) {
                new_x = g_compositor_state->width - xwayland_state->windows[i]->width - WINDOW_BORDER_WIDTH * 2;
            }
            if (new_y > g_compositor_state->height - xwayland_state->windows[i]->height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT) {
                new_y = g_compositor_state->height - xwayland_state->windows[i]->height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT;
            }
            
            // 保存旧位置用于脏标记
            int old_x = xwayland_state->windows[i]->x;
            int old_y = xwayland_state->windows[i]->y;
            int width = xwayland_state->windows[i]->width;
            int height = xwayland_state->windows[i]->height;
            
            // 移动窗口
            xwayland_state->windows[i]->x = new_x;
            xwayland_state->windows[i]->y = new_y;
            xwayland_state->windows[i]->is_dirty = true;
            
            // 标记旧位置和新位置都需要重绘
            mark_dirty_rect(g_compositor_state, old_x, old_y, width, height);
            mark_dirty_rect(g_compositor_state, new_x, new_y, width, height);
            
            return COMPOSITOR_OK;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            // 边界检查
            int new_x = x;
            int new_y = y;
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x > g_compositor_state->width - wayland_state->windows[i]->width - WINDOW_BORDER_WIDTH * 2) {
                new_x = g_compositor_state->width - wayland_state->windows[i]->width - WINDOW_BORDER_WIDTH * 2;
            }
            if (new_y > g_compositor_state->height - wayland_state->windows[i]->height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT) {
                new_y = g_compositor_state->height - wayland_state->windows[i]->height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT;
            }
            
            // 保存旧位置用于脏标记
            int old_x = wayland_state->windows[i]->x;
            int old_y = wayland_state->windows[i]->y;
            int width = wayland_state->windows[i]->width;
            int height = wayland_state->windows[i]->height;
            
            // 移动窗口
            wayland_state->windows[i]->x = new_x;
            wayland_state->windows[i]->y = new_y;
            wayland_state->windows[i]->is_dirty = true;
            
            // 标记旧位置和新位置都需要重绘
            mark_dirty_rect(g_compositor_state, old_x, old_y, width, height);
            mark_dirty_rect(g_compositor_state, new_x, new_y, width, height);
            
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 最小化窗口
int compositor_minimize_window(const char* window_title) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for minimize_window");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找并最小化Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED) {
                // 准备临时WindowInfo用于保存状态
                WindowInfo info = {
                    .title = xwayland_state->windows[i]->title,
                    .x = xwayland_state->windows[i]->x,
                    .y = xwayland_state->windows[i]->y,
                    .width = xwayland_state->windows[i]->width,
                    .height = xwayland_state->windows[i]->height,
                    .state = xwayland_state->windows[i]->state,
                    .opacity = xwayland_state->windows[i]->opacity,
                    .z_order = xwayland_state->windows[i]->z_order,
                    .is_wayland = false
                };
                window_save_state(&info, &xwayland_state->windows[i]->saved_state);
                xwayland_state->windows[i]->state = WINDOW_STATE_MINIMIZED;
                xwayland_state->windows[i]->y = WINDOW_MINIMIZED_Y;
                mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            return COMPOSITOR_OK;
        }
    }
    
    // 查找并最小化Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED) {
                // 准备临时WindowInfo用于保存状态
                WindowInfo info = {
                    .title = wayland_state->windows[i]->title,
                    .x = wayland_state->windows[i]->x,
                    .y = wayland_state->windows[i]->y,
                    .width = wayland_state->windows[i]->width,
                    .height = wayland_state->windows[i]->height,
                    .state = wayland_state->windows[i]->state,
                    .opacity = wayland_state->windows[i]->opacity,
                    .z_order = wayland_state->windows[i]->z_order,
                    .is_wayland = true
                };
                window_save_state(&info, &wayland_state->windows[i]->saved_state);
                wayland_state->windows[i]->state = WINDOW_STATE_MINIMIZED;
                wayland_state->windows[i]->y = WINDOW_MINIMIZED_Y;
                mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 最大化窗口
int compositor_maximize_window(const char* window_title) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for maximize_window");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找并最大化Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            if (xwayland_state->windows[i]->state != WINDOW_STATE_MAXIMIZED) {
                // 准备临时WindowInfo用于保存状态
                WindowInfo info = {
                    .title = xwayland_state->windows[i]->title,
                    .x = xwayland_state->windows[i]->x,
                    .y = xwayland_state->windows[i]->y,
                    .width = xwayland_state->windows[i]->width,
                    .height = xwayland_state->windows[i]->height,
                    .state = xwayland_state->windows[i]->state,
                    .opacity = xwayland_state->windows[i]->opacity,
                    .z_order = xwayland_state->windows[i]->z_order,
                    .is_wayland = false
                };
                window_save_state(&info, &xwayland_state->windows[i]->saved_state);
                xwayland_state->windows[i]->state = WINDOW_STATE_MAXIMIZED;
                xwayland_state->windows[i]->x = WINDOW_MARGIN;
                xwayland_state->windows[i]->y = WINDOW_MARGIN;
                xwayland_state->windows[i]->width = g_compositor_state->width - 2 * WINDOW_MARGIN - 2 * WINDOW_BORDER_WIDTH;
                xwayland_state->windows[i]->height = g_compositor_state->height - 2 * WINDOW_MARGIN - 2 * WINDOW_BORDER_WIDTH - WINDOW_TITLEBAR_HEIGHT;
                mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            return COMPOSITOR_OK;
        }
    }
    
    // 查找并最大化Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            if (wayland_state->windows[i]->state != WINDOW_STATE_MAXIMIZED) {
                // 准备临时WindowInfo用于保存状态
                WindowInfo info = {
                    .title = wayland_state->windows[i]->title,
                    .x = wayland_state->windows[i]->x,
                    .y = wayland_state->windows[i]->y,
                    .width = wayland_state->windows[i]->width,
                    .height = wayland_state->windows[i]->height,
                    .state = wayland_state->windows[i]->state,
                    .opacity = wayland_state->windows[i]->opacity,
                    .z_order = wayland_state->windows[i]->z_order,
                    .is_wayland = true
                };
                window_save_state(&info, &wayland_state->windows[i]->saved_state);
                wayland_state->windows[i]->state = WINDOW_STATE_MAXIMIZED;
                wayland_state->windows[i]->x = WINDOW_MARGIN;
                wayland_state->windows[i]->y = WINDOW_MARGIN;
                wayland_state->windows[i]->width = g_compositor_state->width - 2 * WINDOW_MARGIN - 2 * WINDOW_BORDER_WIDTH;
                wayland_state->windows[i]->height = g_compositor_state->height - 2 * WINDOW_MARGIN - 2 * WINDOW_BORDER_WIDTH - WINDOW_TITLEBAR_HEIGHT;
                mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 还原窗口
int compositor_restore_window(const char* window_title) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for restore_window");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找并还原Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            // 准备临时WindowInfo用于恢复状态
            WindowInfo info;
            window_restore_state(&info, &xwayland_state->windows[i]->saved_state);
            
            // 应用恢复的状态到窗口
            xwayland_state->windows[i]->x = info.x;
            xwayland_state->windows[i]->y = info.y;
            xwayland_state->windows[i]->width = info.width;
            xwayland_state->windows[i]->height = info.height;
            xwayland_state->windows[i]->state = info.state;
            xwayland_state->windows[i]->opacity = info.opacity;
            xwayland_state->windows[i]->z_order = info.z_order;
            
            mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            return COMPOSITOR_OK;
        }
    }
    
    // 查找并还原Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            // 准备临时WindowInfo用于恢复状态
            WindowInfo info;
            window_restore_state(&info, &wayland_state->windows[i]->saved_state);
            
            // 应用恢复的状态到窗口
            wayland_state->windows[i]->x = info.x;
            wayland_state->windows[i]->y = info.y;
            wayland_state->windows[i]->width = info.width;
            wayland_state->windows[i]->height = info.height;
            wayland_state->windows[i]->state = info.state;
            wayland_state->windows[i]->opacity = info.opacity;
            wayland_state->windows[i]->z_order = info.z_order;
            
            mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 设置窗口透明度
int compositor_set_window_opacity(const char* window_title, float opacity) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for set_window_opacity");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    if (opacity < 0.0f || opacity > 1.0f) {
        log_message(COMPOSITOR_LOG_WARN, "Opacity value out of range (0.0-1.0): %.2f", opacity);
        opacity = clamp_float(opacity, 0.0f, 1.0f);
    }
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            // 安全设置透明度
            XwaylandWindow* window = xwayland_state->windows[i];
            window->opacity = opacity;
            
            // 标记需要重绘的区域
            mark_dirty_rect(g_compositor_state, window->x, 
                          window->y, 
                          window->width, 
                          window->height);
            return COMPOSITOR_OK;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            // 安全设置透明度
            WaylandWindow* window = wayland_state->windows[i];
            window->opacity = opacity;
            
            // 标记需要重绘的区域
            mark_dirty_rect(g_compositor_state, window->x, 
                          window->y, 
                          window->width, 
                          window->height);
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 获取窗口信息
int compositor_get_window_info(const char* window_title, WindowInfo* info) {
    if (!g_compositor_state || !window_title || !info) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for get_window_info");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            // 填充窗口信息
            info->title = xwayland_state->windows[i]->title;
            info->x = xwayland_state->windows[i]->x;
            info->y = xwayland_state->windows[i]->y;
            info->width = xwayland_state->windows[i]->width;
            info->height = xwayland_state->windows[i]->height;
            info->state = xwayland_state->windows[i]->state;
            info->opacity = xwayland_state->windows[i]->opacity;
            info->z_order = xwayland_state->windows[i]->z_order;
            info->is_wayland = false;
            return COMPOSITOR_OK;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            // 填充窗口信息
            info->title = wayland_state->windows[i]->title;
            info->x = wayland_state->windows[i]->x;
            info->y = wayland_state->windows[i]->y;
            info->width = wayland_state->windows[i]->width;
            info->height = wayland_state->windows[i]->height;
            info->state = wayland_state->windows[i]->state;
            info->opacity = wayland_state->windows[i]->opacity;
            info->z_order = wayland_state->windows[i]->z_order;
            info->is_wayland = true;
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 获取所有窗口列表
int compositor_get_all_windows(int* count, char*** titles) {
    if (!g_compositor_state || !count || !titles) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for get_all_windows");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 统计窗口总数
    int total_count = 0;
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title) total_count++;
    }
    
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title) total_count++;
    }
    
    // 分配内存
    *titles = (char**)malloc(total_count * sizeof(char*));
    if (!*titles) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for window titles");
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    // 填充窗口标题
    int index = 0;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title) {
            (*titles)[index++] = strdup(xwayland_state->windows[i]->title);
        }
    }
    
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title) {
            (*titles)[index++] = strdup(wayland_state->windows[i]->title);
        }
    }
    
    *count = total_count;
    return COMPOSITOR_OK;
}

// 获取窗口Z顺序
int compositor_get_window_z_order(const char* window_title) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for get_window_z_order");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            return xwayland_state->windows[i]->z_order;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            return wayland_state->windows[i]->z_order;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 设置窗口Z顺序
int compositor_set_window_z_order(const char* window_title, int z_order) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for set_window_z_order");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            xwayland_state->windows[i]->z_order = z_order;
            xwayland_state->windows[i]->is_dirty = true;
            
            // 标记窗口区域需要重绘
            mark_dirty_rect(g_compositor_state, xwayland_state->windows[i]->x, 
                           xwayland_state->windows[i]->y, 
                           xwayland_state->windows[i]->width, 
                           xwayland_state->windows[i]->height);
            
            // 重新排序窗口
            compositor_sort_windows_by_z_order(g_compositor_state);
            
            return COMPOSITOR_OK;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            wayland_state->windows[i]->z_order = z_order;
            wayland_state->windows[i]->is_dirty = true;
            
            // 标记窗口区域需要重绘
            mark_dirty_rect(g_compositor_state, wayland_state->windows[i]->x, 
                           wayland_state->windows[i]->y, 
                           wayland_state->windows[i]->width, 
                           wayland_state->windows[i]->height);
            
            // 重新排序窗口
            compositor_sort_windows_by_z_order(g_compositor_state);
            
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 获取所有窗口信息
int compositor_get_all_windows_info(WindowInfo** windows, int* count) {
    if (!g_compositor_state || !windows || !count) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for get_all_windows_info");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 统计窗口总数
    int total_count = 0;
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    
    total_count = xwayland_state->window_count + wayland_state->window_count;
    
    // 分配内存
    *windows = (WindowInfo*)malloc(total_count * sizeof(WindowInfo));
    if (!*windows) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for window info");
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    // 填充Xwayland窗口信息
    int index = 0;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        WindowInfo* info = &(*windows)[index++];
        info->title = xwayland_state->windows[i]->title;
        info->x = xwayland_state->windows[i]->x;
        info->y = xwayland_state->windows[i]->y;
        info->width = xwayland_state->windows[i]->width;
        info->height = xwayland_state->windows[i]->height;
        info->state = xwayland_state->windows[i]->state;
        info->opacity = xwayland_state->windows[i]->opacity;
        info->z_order = xwayland_state->windows[i]->z_order;
        info->is_wayland = false;
    }
    
    // 填充Wayland窗口信息
    for (int i = 0; i < wayland_state->window_count; i++) {
        WindowInfo* info = &(*windows)[index++];
        info->title = wayland_state->windows[i]->title;
        info->x = wayland_state->windows[i]->x;
        info->y = wayland_state->windows[i]->y;
        info->width = wayland_state->windows[i]->width;
        info->height = wayland_state->windows[i]->height;
        info->state = wayland_state->windows[i]->state;
        info->opacity = wayland_state->windows[i]->opacity;
        info->z_order = wayland_state->windows[i]->z_order;
        info->is_wayland = true;
    }
    
    *count = total_count;
    return COMPOSITOR_OK;
}

// 窗口排序函数 - 根据Z顺序排序窗口
void compositor_sort_windows_by_z_order(CompositorState* state) {
    if (!state) return;
    
    // 排序Xwayland窗口
    XwaylandWindowState* xwayland_state = &state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count - 1; i++) {
        for (int j = 0; j < xwayland_state->window_count - i - 1; j++) {
            if (xwayland_state->windows[j]->z_order > xwayland_state->windows[j + 1]->z_order) {
                // 交换窗口
                XwaylandWindow* temp = xwayland_state->windows[j];
                xwayland_state->windows[j] = xwayland_state->windows[j + 1];
                xwayland_state->windows[j + 1] = temp;
            }
        }
    }
    
    // 排序Wayland窗口
    WaylandWindowState* wayland_state = &state->wayland_state;
    for (int i = 0; i < wayland_state->window_count - 1; i++) {
        for (int j = 0; j < wayland_state->window_count - i - 1; j++) {
            if (wayland_state->windows[j]->z_order > wayland_state->windows[j + 1]->z_order) {
                // 交换窗口
                WaylandWindow* temp = wayland_state->windows[j];
                wayland_state->windows[j] = wayland_state->windows[j + 1];
                wayland_state->windows[j + 1] = temp;
            }
        }
    }
}

// 根据窗口指针获取窗口信息
int compositor_get_window_info_by_ptr(void* window_ptr, bool is_wayland_window, WindowInfo* info) {
    if (!g_compositor_state || !window_ptr || !info) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for get_window_info_by_ptr");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    if (is_wayland_window) {
        WaylandWindow* window = (WaylandWindow*)window_ptr;
        info->title = window->title;
        info->x = window->x;
        info->y = window->y;
        info->width = window->width;
        info->height = window->height;
        info->state = window->state;
        info->opacity = window->opacity;
        info->z_order = window->z_order;
        info->is_wayland = true;
    } else {
        XwaylandWindow* window = (XwaylandWindow*)window_ptr;
        info->title = window->title;
        info->x = window->x;
        info->y = window->y;
        info->width = window->width;
        info->height = window->height;
        info->state = window->state;
        info->opacity = window->opacity;
        info->z_order = window->z_order;
        info->is_wayland = false;
    }
    
    return COMPOSITOR_OK;
}

// 获取活动窗口信息
int compositor_get_active_window_info(WindowInfo* info) {
    if (!g_compositor_state || !info) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for get_active_window_info");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找最高Z顺序的窗口
    int highest_z_order = -1;
    void* active_window = NULL;
    bool is_wayland = false;
    
    // 检查Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            xwayland_state->windows[i]->z_order > highest_z_order) {
            highest_z_order = xwayland_state->windows[i]->z_order;
            active_window = xwayland_state->windows[i];
            is_wayland = false;
        }
    }
    
    // 检查Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            wayland_state->windows[i]->z_order > highest_z_order) {
            highest_z_order = wayland_state->windows[i]->z_order;
            active_window = wayland_state->windows[i];
            is_wayland = true;
        }
    }
    
    if (!active_window) {
        return COMPOSITOR_ERROR_NO_ACTIVE_WINDOW;
    }
    
    return compositor_get_window_info_by_ptr(active_window, is_wayland, info);
}

// 获取窗口数量
int compositor_get_window_count(bool include_wayland, bool include_xwayland) {
    if (!g_compositor_state) {
        log_message(COMPOSITOR_LOG_ERROR, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    int count = 0;
    
    if (include_xwayland) {
        count += g_compositor_state->xwayland_state.window_count;
    }
    
    if (include_wayland) {
        count += g_compositor_state->wayland_state.window_count;
    }
    
    return count;
}

// 标记窗口脏区域
int compositor_mark_window_dirty_region(const char* window_title, int x, int y, int width, int height) {
    if (!g_compositor_state || !window_title) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for mark_window_dirty_region");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            xwayland_state->windows[i]->is_dirty = true;
            
            // 转换为全局坐标并标记脏区域
            int global_x = xwayland_state->windows[i]->x + x;
            int global_y = xwayland_state->windows[i]->y + y;
            mark_dirty_rect(g_compositor_state, global_x, global_y, width, height);
            
            return COMPOSITOR_OK;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            wayland_state->windows[i]->is_dirty = true;
            
            // 转换为全局坐标并标记脏区域
            int global_x = wayland_state->windows[i]->x + x;
            int global_y = wayland_state->windows[i]->y + y;
            mark_dirty_rect(g_compositor_state, global_x, global_y, width, height);
            
            return COMPOSITOR_OK;
        }
    }
    
    return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
}

// 动态扩容窗口数组
static int resize_window_array(void*** array, int32_t* capacity, int32_t new_capacity, size_t element_size) {
    void** new_array = (void**)safe_realloc(*array, element_size * new_capacity);
    if (!new_array) {
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    *array = new_array;
    *capacity = new_capacity;
    return COMPOSITOR_OK;
}

// 添加Xwayland窗口（内部函数）
int add_xwayland_window(XwaylandWindow* window) {
    if (!g_compositor_state || !window) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    XwaylandWindowState* state = &g_compositor_state->xwayland_state;
    
    // 检查是否需要扩容
    if (state->window_count >= state->capacity) {
        int new_capacity = state->capacity * 2;
        if (new_capacity == 0) new_capacity = 8;
        
        // 检查是否超过最大窗口限制
        if (new_capacity > state->max_windows) {
            log_message(COMPOSITOR_LOG_ERROR, "Maximum window count reached");
            return COMPOSITOR_ERROR_MAX_WINDOWS;
        }
        
        if (resize_window_array((void***)&state->windows, &state->capacity, new_capacity, 
                               sizeof(XwaylandWindow*)) != COMPOSITOR_OK) {
            return COMPOSITOR_ERROR_MEMORY;
        }
    }
    
    // 设置Z轴顺序
    window->z_order = g_compositor_state->next_z_order++;
    window->is_dirty = true;
    
    // 初始化保存状态
    window_save_state((WindowInfo*)window, &window->saved_state);
    
    // 添加窗口
    state->windows[state->window_count++] = window;
    
    // 按Z轴顺序排序
    compositor_sort_windows_by_z_order(g_compositor_state);
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    log_message(COMPOSITOR_LOG_DEBUG, "Added Xwayland window: %s, Z-order: %d", 
               window->title ? window->title : "(null)", window->z_order);
    
    return COMPOSITOR_OK;
}

// 添加Wayland窗口（内部函数）
int add_wayland_window(WaylandWindow* window) {
    if (!g_compositor_state || !window) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    WaylandWindowState* state = &g_compositor_state->wayland_state;
    
    // 检查是否需要扩容
    if (state->window_count >= state->capacity) {
        int new_capacity = state->capacity * 2;
        if (new_capacity == 0) new_capacity = 8;
        
        // 检查是否超过最大窗口限制
        if (new_capacity > state->max_windows) {
            log_message(COMPOSITOR_LOG_ERROR, "Maximum window count reached");
            return COMPOSITOR_ERROR_MAX_WINDOWS;
        }
        
        if (resize_window_array((void***)&state->windows, &state->capacity, new_capacity, 
                               sizeof(WaylandWindow*)) != COMPOSITOR_OK) {
            return COMPOSITOR_ERROR_MEMORY;
        }
    }
    
    // 设置Z轴顺序
    window->z_order = g_compositor_state->next_z_order++;
    window->is_dirty = true;
    
    // 初始化保存状态
    window_save_state((WindowInfo*)window, &window->saved_state);
    
    // 添加窗口
    state->windows[state->window_count++] = window;
    
    // 按Z轴顺序排序
    compositor_sort_windows_by_z_order(g_compositor_state);
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    log_message(COMPOSITOR_LOG_DEBUG, "Added Wayland window: %s, Z-order: %d", 
               window->title ? window->title : "(null)", window->z_order);
    
    return COMPOSITOR_OK;
}

// 清理所有窗口
void cleanup_windows(CompositorState* state) {
    if (!state) return;
    
    // 清理Xwayland窗口
    XwaylandWindowState* xwayland_state = &state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]) {
            if (xwayland_state->windows[i]->title) {
                free((void*)xwayland_state->windows[i]->title);
            }
            free(xwayland_state->windows[i]);
        }
    }
    
    if (xwayland_state->windows) {
        free(xwayland_state->windows);
        xwayland_state->windows = NULL;
    }
    xwayland_state->window_count = 0;
    xwayland_state->capacity = 0;
    
    // 清理Wayland窗口
    WaylandWindowState* wayland_state = &state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]) {
            if (wayland_state->windows[i]->title) {
                free((void*)wayland_state->windows[i]->title);
            }
            free(wayland_state->windows[i]);
        }
    }
    
    if (wayland_state->windows) {
        free(wayland_state->windows);
        wayland_state->windows = NULL;
    }
    wayland_state->window_count = 0;
    wayland_state->capacity = 0;
    
    // 重置Z轴顺序
    state->next_z_order = 0;
}

// 查找窗口函数（根据标题）
static void* find_window_by_title(const char* title, bool* out_is_wayland) {
    if (!g_compositor_state || !title) return NULL;
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && 
            strcmp(xwayland_state->windows[i]->title, title) == 0) {
            if (out_is_wayland) *out_is_wayland = false;
            return xwayland_state->windows[i];
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && 
            strcmp(wayland_state->windows[i]->title, title) == 0) {
            if (out_is_wayland) *out_is_wayland = true;
            return wayland_state->windows[i];
        }
    }
    
    return NULL;
}

// 清理窗口管理相关资源
void compositor_window_cleanup(void) {
    g_compositor_state = NULL;
}