#include "compositor_input_shortcuts.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 键盘快捷键初始化
int compositor_input_shortcuts_init(void) {
    return 0;
}

// 键盘快捷键清理
void compositor_input_shortcuts_cleanup(void) {
    // 无需特殊清理
}

// 基本键盘快捷键处理
void handle_keyboard_shortcuts(int keycode, int state, int modifiers) {
    // Alt+Tab 窗口切换
    if (keycode == 23) { // Tab键
        if (state == COMPOSITOR_INPUT_STATE_PRESSED && (modifiers & COMPOSITOR_MODIFIER_ALT)) {
            if (!g_window_switch_state.window_switching) {
                handle_window_switch_start();
            } else {
                handle_window_switch_next();
            }
        } else if (state == COMPOSITOR_INPUT_STATE_RELEASED && g_window_switch_state.window_switching) {
            handle_window_switch_end();
        }
    }
}

// 增强的键盘快捷键处理
void handle_enhanced_keyboard_shortcuts(int keycode, int state, int modifiers) {
    // 先处理基本快捷键
    handle_keyboard_shortcuts(keycode, state, modifiers);
    
    // 工作区快捷键
    if (modifiers == COMPOSITOR_MODIFIER_CTRL_ALT && state == COMPOSITOR_INPUT_STATE_PRESSED) {
        // Ctrl+Alt+数字：切换到对应工作区
        if (keycode >= 10 && keycode <= 19) { // 数字1-0
            int workspace_index = (keycode - 10) % g_compositor_state->workspace_count;
            compositor_switch_workspace(workspace_index);
        }
    }
    
    // Ctrl+Alt+Shift+数字：将当前窗口移动到对应工作区
    if (modifiers == (COMPOSITOR_MODIFIER_CTRL_ALT | COMPOSITOR_MODIFIER_SHIFT) && state == COMPOSITOR_INPUT_STATE_PRESSED) {
        if (keycode >= 10 && keycode <= 19 && g_compositor_state->active_window) { // 数字1-0
            int workspace_index = (keycode - 10) % g_compositor_state->workspace_count;
            compositor_move_window_to_workspace_by_ptr(g_compositor_state->active_window, 
                                                     g_compositor_state->active_window_is_wayland,
                                                     workspace_index);
        }
    }
    
    // 窗口管理快捷键
    if (modifiers == COMPOSITOR_MODIFIER_ALT && state == COMPOSITOR_INPUT_STATE_PRESSED) {
        // Alt+Enter：全屏切换
        if (keycode == 36) {
            if (g_compositor_state->active_window) {
                bool is_fullscreen = false;
                if (g_compositor_state->active_window_is_wayland) {
                    WaylandWindow* window = (WaylandWindow*)g_compositor_state->active_window;
                    is_fullscreen = (window->state == WINDOW_STATE_FULLSCREEN);
                    if (is_fullscreen) {
                        wayland_window_exit_fullscreen(window);
                    } else {
                        wayland_window_enter_fullscreen(window);
                    }
                } else {
                    XwaylandWindowState* window = (XwaylandWindowState*)g_compositor_state->active_window;
                    is_fullscreen = (window->state == WINDOW_STATE_FULLSCREEN);
                    if (is_fullscreen) {
                        xwayland_window_exit_fullscreen(window);
                    } else {
                        xwayland_window_enter_fullscreen(window);
                    }
                }
            }
        }
        
        // Alt+F1：显示应用程序菜单
        else if (keycode == 67) {
            compositor_show_application_menu();
        }
    }
    
    // 平铺快捷键
    if (modifiers == (COMPOSITOR_MODIFIER_SUPER | COMPOSITOR_MODIFIER_SHIFT) && state == COMPOSITOR_INPUT_STATE_PRESSED) {
        switch (keycode) {
            case 111: // 上箭头 - 垂直平铺
                compositor_tile_windows(TILE_MODE_VERTICAL);
                break;
            case 116: // 下箭头 - 水平平铺
                compositor_tile_windows(TILE_MODE_HORIZONTAL);
                break;
            case 32:  // 空格键 - 网格平铺
                compositor_tile_windows(TILE_MODE_GRID);
                break;
        }
    }
}