#include "compositor_window.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

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
            xwayland_state->windows[i]->width = width;
            xwayland_state->windows[i]->height = height;
            mark_dirty_rect(g_compositor_state, xwayland_state->windows[i]->x, 
                           xwayland_state->windows[i]->y, width, height);
            return COMPOSITOR_OK;
        }
    }
    
    // 查找并调整Wayland窗口大小
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            wayland_state->windows[i]->width = width;
            wayland_state->windows[i]->height = height;
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
            
            // 移动窗口
            xwayland_state->windows[i]->x = new_x;
            xwayland_state->windows[i]->y = new_y;
            mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
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
            
            // 移动窗口
            wayland_state->windows[i]->x = new_x;
            wayland_state->windows[i]->y = new_y;
            mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
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
                window_save_state((WindowInfo*)xwayland_state->windows[i], &xwayland_state->windows[i]->saved_state);
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
                window_save_state((WindowInfo*)wayland_state->windows[i], &wayland_state->windows[i]->saved_state);
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
                window_save_state((WindowInfo*)xwayland_state->windows[i], &xwayland_state->windows[i]->saved_state);
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
                window_save_state((WindowInfo*)wayland_state->windows[i], &wayland_state->windows[i]->saved_state);
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
            window_restore_state((WindowInfo*)xwayland_state->windows[i], &xwayland_state->windows[i]->saved_state);
            mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            return COMPOSITOR_OK;
        }
    }
    
    // 查找并还原Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            window_restore_state((WindowInfo*)wayland_state->windows[i], &wayland_state->windows[i]->saved_state);
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
    
    // 边界检查
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    
    // 查找并设置Xwayland窗口透明度
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && strcmp(xwayland_state->windows[i]->title, window_title) == 0) {
            xwayland_state->windows[i]->opacity = opacity;
            mark_dirty_rect(g_compositor_state, xwayland_state->windows[i]->x, 
                           xwayland_state->windows[i]->y, 
                           xwayland_state->windows[i]->width, 
                           xwayland_state->windows[i]->height);
            return COMPOSITOR_OK;
        }
    }
    
    // 查找并设置Wayland窗口透明度
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && strcmp(wayland_state->windows[i]->title, window_title) == 0) {
            wayland_state->windows[i]->opacity = opacity;
            mark_dirty_rect(g_compositor_state, wayland_state->windows[i]->x, 
                           wayland_state->windows[i]->y, 
                           wayland_state->windows[i]->width, 
                           wayland_state->windows[i]->height);
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

// 清理窗口管理相关资源
void compositor_window_cleanup(void) {
    g_compositor_state = NULL;
}