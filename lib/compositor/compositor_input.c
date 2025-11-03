#include "compositor_input.h"
#include "compositor_window.h"
#include "compositor_utils.h"
#include <string.h>

// 定义全局状态指针，实际实现时需要从compositor.c获取
static CompositorState* g_compositor_state = NULL;

// Alt+Tab功能相关静态变量
static bool g_alt_key_pressed = false;
static bool g_window_switching = false;
static int g_selected_window_index = 0;
static void** g_window_list = NULL;
static int g_window_list_count = 0;
static bool* g_window_is_wayland_list = NULL;

// 设置全局状态指针（由compositor.c调用）
void compositor_input_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 清理窗口列表资源
static void cleanup_window_list(void) {
    if (g_window_list != NULL) {
        free(g_window_list);
        g_window_list = NULL;
    }
    if (g_window_is_wayland_list != NULL) {
        free(g_window_is_wayland_list);
        g_window_is_wayland_list = NULL;
    }
    g_window_list_count = 0;
    g_selected_window_index = 0;
}

// 收集所有可见窗口
static void collect_visible_windows(void) {
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
        g_window_list = (void**)malloc(count * sizeof(void*));
        g_window_is_wayland_list = (bool*)malloc(count * sizeof(bool));
        g_window_list_count = 0;
        
        // 填充Xwayland窗口
        for (int i = 0; i < xwayland_state->window_count; i++) {
            if (xwayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                xwayland_state->windows[i]->surface != NULL) {
                g_window_list[g_window_list_count] = xwayland_state->windows[i];
                g_window_is_wayland_list[g_window_list_count] = false;
                g_window_list_count++;
            }
        }
        
        // 填充Wayland窗口
        for (int i = 0; i < wayland_state->window_count; i++) {
            if (wayland_state->windows[i]->state != WINDOW_STATE_MINIMIZED && 
                wayland_state->windows[i]->surface != NULL) {
                g_window_list[g_window_list_count] = wayland_state->windows[i];
                g_window_is_wayland_list[g_window_list_count] = true;
                g_window_list_count++;
            }
        }
    }
}

// 高亮显示选中的窗口
static void highlight_selected_window(void) {
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
    if (g_window_list_count > 0 && g_selected_window_index >= 0 && g_selected_window_index < g_window_list_count) {
        for (int i = 0; i < g_window_list_count; i++) {
            if (i != g_selected_window_index) {
                if (g_window_is_wayland_list[i]) {
                    WaylandWindow* window = (WaylandWindow*)g_window_list[i];
                    window->opacity = 0.4f;
                } else {
                    XwaylandWindowState* window = (XwaylandWindowState*)g_window_list[i];
                    window->opacity = 0.4f;
                }
            }
        }
    }
}

// 激活选中的窗口
static void activate_selected_window(void) {
    if (g_window_list_count > 0 && g_selected_window_index >= 0 && g_selected_window_index < g_window_list_count) {
        if (g_window_is_wayland_list[g_selected_window_index]) {
            WaylandWindow* window = (WaylandWindow*)g_window_list[g_selected_window_index];
            // 激活Wayland窗口的代码
            wayland_window_activate(window);
        } else {
            XwaylandWindowState* window = (XwaylandWindowState*)g_window_list[g_selected_window_index];
            // 激活Xwayland窗口的代码
            xwayland_window_activate(window);
        }
    }
}

// 输入处理函数实现
void compositor_handle_input(int type, int x, int y, int key, int state) {
    if (!g_compositor_state) {
        log_message(COMPOSITOR_LOG_ERROR, "Compositor not initialized, cannot handle input");
        return;
    }
    
    // 记录输入事件（调试用）
    if (g_compositor_state->config.debug_mode) {
        log_message(COMPOSITOR_LOG_DEBUG, "Input event: type=%d, x=%d, y=%d, key=%d, state=%d", 
                   type, x, y, key, state);
    }
    
    // 处理不同类型的输入事件
    switch (type) {
        case COMPOSITOR_INPUT_MOTION: {
            // 处理鼠标移动事件
            // 拖动窗口逻辑
            if (g_compositor_state->dragging && g_compositor_state->drag_window) {
                // 计算新位置
                int new_x = g_compositor_state->drag_start_x + (x - g_compositor_state->mouse_start_x);
                int new_y = g_compositor_state->drag_start_y + (y - g_compositor_state->mouse_start_y);
                
                // 边界检查
                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                if (new_x > g_compositor_state->width - g_compositor_state->drag_window_width - WINDOW_BORDER_WIDTH * 2) {
                    new_x = g_compositor_state->width - g_compositor_state->drag_window_width - WINDOW_BORDER_WIDTH * 2;
                }
                if (new_y > g_compositor_state->height - g_compositor_state->drag_window_height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT) {
                    new_y = g_compositor_state->height - g_compositor_state->drag_window_height - WINDOW_BORDER_WIDTH * 2 - WINDOW_TITLEBAR_HEIGHT;
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
                mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            break;
        }
        
        case COMPOSITOR_INPUT_BUTTON: {
            // 处理鼠标按钮事件
            // 这里可以实现窗口点击、拖动等功能
            break;
        }
        
        case COMPOSITOR_INPUT_KEY: {
            // 处理键盘事件
            // Alt键处理
            if (key == 56 || key == 184) { // Alt键 (左右)
                if (state == COMPOSITOR_INPUT_STATE_DOWN) {
                    g_alt_key_pressed = true;
                } else if (state == COMPOSITOR_INPUT_STATE_UP) {
                    g_alt_key_pressed = false;
                    
                    // 当Alt键释放时，如果正在窗口切换状态，激活选中的窗口
                    if (g_window_switching) {
                        activate_selected_window();
                        g_window_switching = false;
                        cleanup_window_list();
                        
                        // 重置所有窗口透明度
                        XwaylandWindowState* xwayland_state = &g_compositor_state->xwayland_state;
                        for (int i = 0; i < xwayland_state->window_count; i++) {
                            xwayland_state->windows[i]->opacity = 1.0f;
                        }
                        
                        WaylandWindowState* wayland_state = &g_compositor_state->wayland_state;
                        for (int i = 0; i < wayland_state->window_count; i++) {
                            wayland_state->windows[i]->opacity = 1.0f;
                        }
                        
                        mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
                    }
                }
            }
            
            // Tab键处理 (Alt+Tab)
            if (key == 15 && state == COMPOSITOR_INPUT_STATE_DOWN && g_alt_key_pressed) {
                // 开始窗口切换或切换到下一个窗口
                if (!g_window_switching) {
                    g_window_switching = true;
                    collect_visible_windows();
                    g_selected_window_index = 0;
                } else {
                    // 循环选择下一个窗口
                    g_selected_window_index = (g_selected_window_index + 1) % g_window_list_count;
                }
                
                // 高亮显示选中的窗口
                highlight_selected_window();
                mark_dirty_rect(g_compositor_state, 0, 0, g_compositor_state->width, g_compositor_state->height);
            }
            
            // Alt+F4 关闭窗口
            if (key == 62 && state == COMPOSITOR_INPUT_STATE_DOWN && g_alt_key_pressed) {
                // 关闭当前活动窗口
                if (g_compositor_state->active_window) {
                    if (g_compositor_state->active_window_is_wayland) {
                        WaylandWindow* window = (WaylandWindow*)g_compositor_state->active_window;
                        wayland_window_close(window);
                    } else {
                        XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state->active_window;
                        xwayland_window_close(window);
                    }
                }
            }
            break;
        }
        
        case COMPOSITOR_INPUT_TOUCH: {
            // 处理触摸事件
            // 将触摸事件映射到鼠标事件
            // 这里简化处理，实际可能需要更复杂的多点触摸支持
            break;
        }
    }
}

// 清理输入相关资源
void compositor_input_cleanup(void) {
    cleanup_window_list();
    g_alt_key_pressed = false;
    g_window_switching = false;
    g_compositor_state = NULL;
}