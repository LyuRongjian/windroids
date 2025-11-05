/*
 * WinDroids Compositor
 * Window Switching Implementation
 */

#include "compositor_input_window_switch.h"
#include "compositor_utils.h"
#include "compositor_window.h"
#include <string.h>
#include <stdlib.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// Alt+Tab功能相关静态变量
static bool g_alt_key_pressed = false;
static bool g_window_switching = false;
static int g_selected_window_index = 0;
static void** g_window_list = NULL;
static int g_window_list_count = 0;
static bool* g_window_is_wayland_list = NULL;

// 设置合成器状态引用
void compositor_input_window_switch_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 初始化窗口切换系统
int compositor_input_window_switch_init(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 初始化状态
    g_alt_key_pressed = false;
    g_window_switching = false;
    g_selected_window_index = 0;
    g_window_list = NULL;
    g_window_list_count = 0;
    g_window_is_wayland_list = NULL;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Window switching system initialized");
    return COMPOSITOR_OK;
}

// 清理窗口切换系统
void compositor_input_window_switch_cleanup(void) {
    cleanup_window_list();
    log_message(COMPOSITOR_LOG_DEBUG, "Window switching system cleaned up");
}

// 清理窗口列表
void cleanup_window_list(void) {
    if (g_window_list) {
        free(g_window_list);
        g_window_list = NULL;
    }
    if (g_window_is_wayland_list) {
        free(g_window_is_wayland_list);
        g_window_is_wayland_list = NULL;
    }
    g_window_list_count = 0;
    g_selected_window_index = 0;
}

// 收集所有可见窗口
void collect_visible_windows(void) {
    cleanup_window_list();
    
    if (!g_compositor_state) {
        return;
    }
    
    // 统计窗口数量
    int count = 0;
    
    // 检查Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i] && 
            xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            xwayland_state->windows[i]->surface != NULL) {
            count++;
        }
    }
    
    // 检查Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i] && 
            wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            wayland_state->windows[i]->surface != NULL) {
            count++;
        }
    }
    
    // 分配内存
    if (count > 0) {
        g_window_list = (void**)malloc(count * sizeof(void*));
        g_window_is_wayland_list = (bool*)malloc(count * sizeof(bool));
        if (!g_window_list || !g_window_is_wayland_list) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory for window list");
            cleanup_window_list();
            return;
        }
        
        g_window_list_count = 0;
        
        // 填充Xwayland窗口
        for (int i = 0; i < xwayland_state->window_count; i++) {
            if (xwayland_state->windows[i] && 
                xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                xwayland_state->windows[i]->surface != NULL) {
                g_window_list[g_window_list_count] = xwayland_state->windows[i];
                g_window_is_wayland_list[g_window_list_count] = false;
                g_window_list_count++;
            }
        }
        
        // 填充Wayland窗口
        for (int i = 0; i < wayland_state->window_count; i++) {
            if (wayland_state->windows[i] && 
                wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                wayland_state->windows[i]->surface != NULL) {
                g_window_list[g_window_list_count] = wayland_state->windows[i];
                g_window_is_wayland_list[g_window_list_count] = true;
                g_window_list_count++;
            }
        }
        
        log_message(COMPOSITOR_LOG_DEBUG, "Collected %d visible windows", g_window_list_count);
    }
}

// 开始窗口切换（Alt+Tab）
int compositor_input_start_window_switch(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 收集可见窗口
    collect_visible_windows();
    
    if (g_window_list_count == 0) {
        log_message(COMPOSITOR_LOG_WARN, "No windows available for switching");
        return COMPOSITOR_ERROR_NO_WINDOWS;
    }
    
    g_window_switching = true;
    g_selected_window_index = 0;
    
    // 显示窗口预览
    compositor_input_show_window_previews();
    
    log_message(COMPOSITOR_LOG_DEBUG, "Started window switching mode");
    return COMPOSITOR_OK;
}

// 结束窗口切换
int compositor_input_end_window_switch(bool apply_selection) {
    if (!g_window_switching) {
        return COMPOSITOR_ERROR_INVALID_STATE;
    }
    
    // 隐藏窗口预览
    compositor_input_hide_window_previews();
    
    if (apply_selection && g_window_list_count > 0 && g_selected_window_index >= 0 && 
        g_selected_window_index < g_window_list_count) {
        // 激活选中的窗口
        void* window = g_window_list[g_selected_window_index];
        bool is_wayland = g_window_is_wayland_list[g_selected_window_index];
        
        // 设置窗口为活动状态并提升Z轴顺序
        if (is_wayland) {
            WaylandWindow* wl_window = (WaylandWindow*)window;
            wl_window->z_order = g_compositor_state->next_z_order++;
            wl_window->is_active = true;
            log_message(COMPOSITOR_LOG_INFO, "Activated Wayland window: %s", 
                       wl_window->title ? wl_window->title : "(null)");
        } else {
            XwaylandWindowState* xwl_window = (XwaylandWindowState*)window;
            xwl_window->z_order = g_compositor_state->next_z_order++;
            xwl_window->is_active = true;
            log_message(COMPOSITOR_LOG_INFO, "Activated Xwayland window: %s", 
                       xwl_window->title ? xwl_window->title : "(null)");
        }
        
        // 按Z轴顺序重新排序窗口
        compositor_sort_windows_by_z_order();
        
        // 标记需要重绘
        compositor_schedule_redraw();
    }
    
    // 清理状态
    g_window_switching = false;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Ended window switching mode");
    return COMPOSITOR_OK;
}

// 选择下一个窗口
int compositor_input_select_next_window(void) {
    if (!g_window_switching || g_window_list_count <= 0) {
        return COMPOSITOR_ERROR_INVALID_STATE;
    }
    
    g_selected_window_index = (g_selected_window_index + 1) % g_window_list_count;
    
    // 更新窗口预览
    compositor_input_show_window_previews();
    
    log_message(COMPOSITOR_LOG_DEBUG, "Selected window index: %d", g_selected_window_index);
    return COMPOSITOR_OK;
}

// 选择上一个窗口
int compositor_input_select_prev_window(void) {
    if (!g_window_switching || g_window_list_count <= 0) {
        return COMPOSITOR_ERROR_INVALID_STATE;
    }
    
    g_selected_window_index = (g_selected_window_index - 1 + g_window_list_count) % g_window_list_count;
    
    // 更新窗口预览
    compositor_input_show_window_previews();
    
    log_message(COMPOSITOR_LOG_DEBUG, "Selected window index: %d", g_selected_window_index);
    return COMPOSITOR_OK;
}

// 获取当前选中的窗口索引
int compositor_input_get_selected_window_index(void) {
    return g_selected_window_index;
}

// 获取窗口列表
void** compositor_input_get_window_list(int* out_count, bool** out_is_wayland) {
    if (out_count) {
        *out_count = g_window_list_count;
    }
    if (out_is_wayland) {
        *out_is_wayland = g_window_is_wayland_list;
    }
    return g_window_list;
}

// 显示窗口预览
int compositor_input_show_window_previews(void) {
    if (!g_compositor_state || g_window_list_count <= 0) {
        return COMPOSITOR_ERROR_INVALID_STATE;
    }
    
    // 这里应该实现窗口预览的显示逻辑
    // 例如：创建预览窗口、显示缩略图等
    
    log_message(COMPOSITOR_LOG_DEBUG, "Showing window previews, selected index: %d", g_selected_window_index);
    
    // 标记需要重绘以显示预览
    compositor_schedule_redraw();
    
    return COMPOSITOR_OK;
}

// 隐藏窗口预览
void compositor_input_hide_window_previews(void) {
    // 这里应该实现窗口预览的隐藏逻辑
    
    log_message(COMPOSITOR_LOG_DEBUG, "Hiding window previews");
    
    // 标记需要重绘以清除预览
    compositor_schedule_redraw();
}