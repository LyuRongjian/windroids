#include "compositor_window_utils.h"
#include "compositor_utils.h"
#include <string.h>

// 根据标题查找窗口
WindowSearchResult compositor_find_window_by_title(const char* title) {
    WindowSearchResult result = {0};
    
    if (!g_compositor_state || !title) {
        return result;
    }
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->title && 
            strcmp(xwayland_state->windows[i]->title, title) == 0) {
            result.window = xwayland_state->windows[i];
            result.is_wayland = false;
            result.z_order = xwayland_state->windows[i]->z_order;
            return result;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->title && 
            strcmp(wayland_state->windows[i]->title, title) == 0) {
            result.window = wayland_state->windows[i];
            result.is_wayland = true;
            result.z_order = wayland_state->windows[i]->z_order;
            return result;
        }
    }
    
    return result;
}

// 根据位置查找窗口
WindowSearchResult compositor_find_window_at_position(int x, int y) {
    WindowSearchResult result = {0};
    
    if (!g_compositor_state) {
        return result;
    }
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        XwaylandWindowState* window = xwayland_state->windows[i];
        if (x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            result.window = window;
            result.is_wayland = false;
            result.z_order = window->z_order;
            return result;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        WaylandWindowState* window = wayland_state->windows[i];
        if (x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            result.window = window;
            result.is_wayland = true;
            result.z_order = window->z_order;
            return result;
        }
    }
    
    return result;
}

// 查找活动窗口（Z顺序最高的窗口）
WindowSearchResult compositor_find_active_window(void) {
    WindowSearchResult result = {0};
    
    if (!g_compositor_state) {
        return result;
    }
    
    // 查找Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        XwaylandWindowState* window = xwayland_state->windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && window->z_order > result.z_order) {
            result.window = window;
            result.is_wayland = false;
            result.z_order = window->z_order;
        }
    }
    
    // 查找Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        WaylandWindowState* window = wayland_state->windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && window->z_order > result.z_order) {
            result.window = window;
            result.is_wayland = true;
            result.z_order = window->z_order;
        }
    }
    
    return result;
}

// 对每个窗口执行回调函数
void compositor_for_each_window(WindowCallback callback, void* user_data) {
    if (!g_compositor_state || !callback) {
        return;
    }
    
    // 遍历Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (!callback(xwayland_state->windows[i], false, user_data)) {
            return; // 回调返回false，停止迭代
        }
    }
    
    // 遍历Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (!callback(wayland_state->windows[i], true, user_data)) {
            return; // 回调返回false，停止迭代
        }
    }
}

// 窗口排序辅助函数
int compositor_compare_window_z_order(const void* a, const void* b) {
    // 这里假设a和b是WindowSearchResult指针
    const WindowSearchResult* win_a = a;
    const WindowSearchResult* win_b = b;
    
    // 按z_order升序排序（小的在下面）
    return win_a->z_order - win_b->z_order;
}

// 检查窗口是否最小化
bool compositor_is_window_minimized(void* window, bool is_wayland) {
    if (!window) {
        return false;
    }
    
    if (is_wayland) {
        WaylandWindowState* wayland_window = (WaylandWindowState*)window;
        return wayland_window->state == WINDOW_STATE_MINIMIZED;
    } else {
        XwaylandWindowState* xwayland_window = (XwaylandWindowState*)window;
        return xwayland_window->state == WINDOW_STATE_MINIMIZED;
    }
}

// 检查窗口是否最大化
bool compositor_is_window_maximized(void* window, bool is_wayland) {
    if (!window) {
        return false;
    }
    
    if (is_wayland) {
        WaylandWindowState* wayland_window = (WaylandWindowState*)window;
        return wayland_window->state == WINDOW_STATE_MAXIMIZED;
    } else {
        XwaylandWindowState* xwayland_window = (XwaylandWindowState*)window;
        return xwayland_window->state == WINDOW_STATE_MAXIMIZED;
    }
}

// 检查窗口是否可见
bool compositor_is_window_visible(void* window, bool is_wayland) {
    if (!window) {
        return false;
    }
    
    if (is_wayland) {
        WaylandWindowState* wayland_window = (WaylandWindowState*)window;
        return wayland_window->state != WINDOW_STATE_MINIMIZED && wayland_window->mapped;
    } else {
        XwaylandWindowState* xwayland_window = (XwaylandWindowState*)window;
        return xwayland_window->state != WINDOW_STATE_MINIMIZED && xwayland_window->mapped;
    }
}

// 获取窗口几何信息
void compositor_get_window_geometry(void* window, bool is_wayland, int* x, int* y, int* width, int* height) {
    if (!window) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    
    if (is_wayland) {
        WaylandWindowState* wayland_window = (WaylandWindowState*)window;
        if (x) *x = wayland_window->x;
        if (y) *y = wayland_window->y;
        if (width) *width = wayland_window->width;
        if (height) *height = wayland_window->height;
    } else {
        XwaylandWindowState* xwayland_window = (XwaylandWindowState*)window;
        if (x) *x = xwayland_window->x;
        if (y) *y = xwayland_window->y;
        if (width) *width = xwayland_window->width;
        if (height) *height = xwayland_window->height;
    }
}

// 获取窗口标题
const char* compositor_get_window_title(void* window, bool is_wayland) {
    if (!window) {
        return NULL;
    }
    
    if (is_wayland) {
        WaylandWindowState* wayland_window = (WaylandWindowState*)window;
        return wayland_window->title;
    } else {
        XwaylandWindowState* xwayland_window = (XwaylandWindowState*)window;
        return xwayland_window->title;
    }
}

// 标记窗口区域为脏
void compositor_mark_window_dirty(void* window, bool is_wayland) {
    if (!window || !g_compositor_state) {
        return;
    }
    
    int x, y, width, height;
    compositor_get_window_geometry(window, is_wayland, &x, &y, &width, &height);
    
    if (width > 0 && height > 0) {
        mark_dirty_rect(g_compositor_state, x, y, width, height);
        
        // 更新窗口的脏状态
        if (is_wayland) {
            WaylandWindowState* wayland_window = (WaylandWindowState*)window;
            wayland_window->is_dirty = false; // 已处理脏状态
        } else {
            XwaylandWindowState* xwayland_window = (XwaylandWindowState*)window;
            xwayland_window->is_dirty = false; // 已处理脏状态
        }
    }
}

// 更新窗口Z顺序
void compositor_update_window_z_order(void* window, bool is_wayland, int z_order) {
    if (!window) {
        return;
    }
    
    if (is_wayland) {
        WaylandWindowState* wayland_window = (WaylandWindowState*)window;
        wayland_window->z_order = z_order;
    } else {
        XwaylandWindowState* xwayland_window = (XwaylandWindowState*)window;
        xwayland_window->z_order = z_order;
    }
}