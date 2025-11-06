#include "compositor_input_window_interaction.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// Alt+Tab功能相关状态
static CompositorWindowSwitchState g_window_switch_state = {0};

// 获取当前时间（毫秒）
static int64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 窗口交互初始化
int compositor_window_interaction_init(void) {
    memset(&g_window_switch_state, 0, sizeof(g_window_switch_state));
    return COMPOSITOR_OK;
}

// 窗口交互清理
void compositor_window_interaction_cleanup(void) {
    cleanup_window_list();
    memset(&g_window_switch_state, 0, sizeof(g_window_switch_state));
}

// 查找指定位置的表面
void* find_surface_at_position(int x, int y, bool* is_wayland) {
    if (!g_compositor_state || !is_wayland) {
        return NULL;
    }
    
    // 先检查Wayland窗口（从顶层开始）
    for (int i = g_compositor_state->wayland_state.window_count - 1; i >= 0; i--) {
        WaylandWindow* window = g_compositor_state->wayland_state.windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            *is_wayland = true;
            return window;
        }
    }
    
    // 再检查Xwayland窗口
    for (int i = g_compositor_state->xwayland_state.window_count - 1; i >= 0; i--) {
        XwaylandWindowState* window = g_compositor_state->xwayland_state.windows[i];
        if (window->state != WINDOW_STATE_MINIMIZED && 
            x >= window->x && x < window->x + window->width &&
            y >= window->y && window->y + window->height) {
            *is_wayland = false;
            return window;
        }
    }
    
    return NULL;
}

// 清理窗口列表资源
void cleanup_window_list(void) {
    if (g_window_switch_state.window_list != NULL) {
        free(g_window_switch_state.window_list);
        g_window_switch_state.window_list = NULL;
    }
    if (g_window_switch_state.window_is_wayland_list != NULL) {
        free(g_window_switch_state.window_is_wayland_list);
        g_window_switch_state.window_is_wayland_list = NULL;
    }
    g_window_switch_state.window_list_count = 0;
    g_window_switch_state.selected_window_index = 0;
}

// 收集所有可见窗口
void collect_visible_windows(void) {
    cleanup_window_list();
    
    // 统计窗口数量
    int count = 0;
    
    // 检查Xwayland窗口
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            xwayland_state->windows[i]->surface != NULL) {
            count++;
        }
    }
    
    // 检查Wayland窗口
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
            wayland_state->windows[i]->surface != NULL) {
            count++;
        }
    }
    
    // 分配内存
    if (count > 0) {
        g_window_switch_state.window_list = (void**)malloc(count * sizeof(void*));
        g_window_switch_state.window_is_wayland_list = (bool*)malloc(count * sizeof(bool));
        g_window_switch_state.window_list_count = 0;
        
        // 填充Xwayland窗口
        for (int i = 0; i < xwayland_state->window_count; i++) {
            if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                xwayland_state->windows[i]->surface != NULL) {
                g_window_switch_state.window_list[g_window_switch_state.window_list_count] = xwayland_state->windows[i];
                g_window_switch_state.window_is_wayland_list[g_window_switch_state.window_list_count] = false;
                g_window_switch_state.window_list_count++;
            }
        }
        
        // 填充Wayland窗口
        for (int i = 0; i < wayland_state->window_count; i++) {
            if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                wayland_state->windows[i]->surface != NULL) {
                g_window_switch_state.window_list[g_window_switch_state.window_list_count] = wayland_state->windows[i];
                g_window_switch_state.window_is_wayland_list[g_window_switch_state.window_list_count] = true;
                g_window_switch_state.window_list_count++;
            }
        }
    }
}

// 高亮显示选中的窗口
void highlight_selected_window(void) {
    // 重置所有窗口透明度
    XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        xwayland_state->windows[i]->opacity = 1.0f;
    }
    
    WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        wayland_state->windows[i]->opacity = 1.0f;
    }
    
    // 降低非选中窗口的透明度
    if (g_window_switch_state.window_list_count > 0 && 
        g_window_switch_state.selected_window_index >= 0 && 
        g_window_switch_state.selected_window_index < g_window_switch_state.window_list_count) {
        for (int i = 0; i < g_window_switch_state.window_list_count; i++) {
            if (i != g_window_switch_state.selected_window_index) {
                if (g_window_switch_state.window_is_wayland_list[i]) {
                    WaylandWindow* window = (WaylandWindow*)g_window_switch_state.window_list[i];
                    window->opacity = 0.4f;
                } else {
                    XwaylandWindowState* window = (XwaylandWindowState*)g_window_switch_state.window_list[i];
                    window->opacity = 0.4f;
                }
            }
        }
    }
}

// 激活选中的窗口
void activate_selected_window(void) {
    if (g_window_switch_state.window_list_count > 0 && 
        g_window_switch_state.selected_window_index >= 0 && 
        g_window_switch_state.selected_window_index < g_window_switch_state.window_list_count) {
        if (g_window_switch_state.window_is_wayland_list[g_window_switch_state.selected_window_index]) {
            WaylandWindow* wayland_window = (WaylandWindow*)g_window_switch_state.window_list[g_window_switch_state.selected_window_index];
            
            // 激活Wayland窗口
            wayland_window_activate(wayland_window);
            
            // 更新全局活动窗口信息
            if (g_compositor_state) {
                g_compositor_state->active_window = wayland_window;
                g_compositor_state->active_window_is_wayland = true;
            }
        } else {
            XwaylandWindowState* xwayland_window = (XwaylandWindowState*)g_window_switch_state.window_list[g_window_switch_state.selected_window_index];
            
            // 激活Xwayland窗口
            xwayland_window_activate(xwayland_window);
            
            // 更新全局活动窗口信息
            if (g_compositor_state) {
                g_compositor_state->active_window = xwayland_window;
                g_compositor_state->active_window_is_wayland = false;
            }
        }
    }
}

// 开始窗口切换
void handle_window_switch_start(void) {
    if (!g_window_switch_state.window_switching) {
        // 开始窗口切换
        g_window_switch_state.window_switching = true;
        collect_visible_windows();
        g_window_switch_state.selected_window_index = 0;
        highlight_selected_window();
        compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
    }
}

// 切换到下一个窗口
void handle_window_switch_next(void) {
    if (g_window_switch_state.window_switching && g_window_switch_state.window_list_count > 0) {
        g_window_switch_state.selected_window_index = (g_window_switch_state.selected_window_index + 1) % g_window_switch_state.window_list_count;
        highlight_selected_window();
        compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
    }
}

// 结束窗口切换
void handle_window_switch_end(void) {
    if (g_window_switch_state.window_switching) {
        activate_selected_window();
        g_window_switch_state.window_switching = false;
        
        // 重置所有窗口透明度
        XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
        for (int i = 0; i < xwayland_state->window_count; i++) {
            xwayland_state->windows[i]->opacity = 1.0f;
        }
        
        WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
        for (int i = 0; i < wayland_state->window_count; i++) {
            wayland_state->windows[i]->opacity = 1.0f;
        }
        
        cleanup_window_list();
        compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
    }
}

// 增强的窗口拖动处理
void handle_window_drag(int x, int y) {
    if (!g_compositor_state->dragging || !g_compositor_state->drag_window) {
        return;
    }
    
    // 计算新位置
    int new_x = g_compositor_state->drag_start_x + (x - g_compositor_state->mouse_start_x);
    int new_y = g_compositor_state->drag_start_y + (y - g_compositor_state->mouse_start_y);
    
    // 边界检查
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    int max_width = g_compositor_state->width - g_compositor_state->drag_window_width - WINDOW_BORDER_WIDTH * 2;
    int max_height = g_compositor_state->height - g_compositor_state->drag_window_height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT;
    if (new_x > max_width) new_x = max_width;
    if (new_y > max_height) new_y = max_height;
    
    // 工作区边缘检测：在边缘停留触发工作区切换
    const int EDGE_THRESHOLD = 50; // 边缘阈值（像素）
    const int EDGE_DELAY = 500;    // 触发延迟（毫秒）
    static int64_t edge_enter_time = 0;
    static int edge_workspace = -1;
    
    // 检查是否在左边缘
    if (new_x < EDGE_THRESHOLD && g_compositor_state->config.wraparound_workspaces) {
        if (edge_workspace != (g_compositor_state->active_workspace - 1 + g_compositor_state->workspace_count) % g_compositor_state->workspace_count) {
            edge_enter_time = get_current_time_ms();
            edge_workspace = (g_compositor_state->active_workspace - 1 + g_compositor_state->workspace_count) % g_compositor_state->workspace_count;
        } else if (get_current_time_ms() - edge_enter_time > EDGE_DELAY) {
            // 切换工作区并继续拖动
            compositor_switch_workspace(edge_workspace);
            // 调整窗口位置到新工作区右侧
            new_x = g_compositor_state->width - g_compositor_state->drag_window_width - WINDOW_BORDER_WIDTH * 2 - EDGE_THRESHOLD;
            g_compositor_state->drag_start_x = new_x;
            g_compositor_state->mouse_start_x = x;
        }
    }
    // 检查是否在右边缘
    else if (new_x > max_width - EDGE_THRESHOLD && g_compositor_state->config.wraparound_workspaces) {
        if (edge_workspace != (g_compositor_state->active_workspace + 1) % g_compositor_state->workspace_count) {
            edge_enter_time = get_current_time_ms();
            edge_workspace = (g_compositor_state->active_workspace + 1) % g_compositor_state->workspace_count;
        } else if (get_current_time_ms() - edge_enter_time > EDGE_DELAY) {
            // 切换工作区并继续拖动
            compositor_switch_workspace(edge_workspace);
            // 调整窗口位置到新工作区左侧
            new_x = EDGE_THRESHOLD;
            g_compositor_state->drag_start_x = new_x;
            g_compositor_state->mouse_start_x = x;
        }
    } else {
        edge_workspace = -1;
    }
    
    // 更新窗口位置
    if (g_compositor_state->drag_is_wayland_window) {
        WaylandWindow* window = (WaylandWindow*)g_compositor_state->drag_window;
        window->x = new_x;
        window->y = new_y;
    } else {
        XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state->drag_window;
        window->x = new_x;
        window->y = new_y;
    }
    
    // 标记脏区域
    compositor_mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
}