#ifndef COMPOSITOR_INPUT_WINDOW_INTERACTION_H
#define COMPOSITOR_INPUT_WINDOW_INTERACTION_H

#include "compositor_input_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Alt+Tab功能相关状态
typedef struct {
    bool alt_key_pressed;
    bool window_switching;
    int selected_window_index;
    void** window_list;
    int window_list_count;
    bool* window_is_wayland_list;
} CompositorWindowSwitchState;

// 窗口交互初始化和清理
int compositor_window_interaction_init(void);
void compositor_window_interaction_cleanup(void);

// 窗口交互处理函数
void handle_window_drag(int x, int y);
void handle_window_switch_start(void);
void handle_window_switch_next(void);
void handle_window_switch_end(void);

// 窗口查找和操作
void* find_surface_at_position(int x, int y, bool* is_wayland);
void collect_visible_windows(void);
void highlight_selected_window(void);
void activate_selected_window(void);
void cleanup_window_list(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_INPUT_WINDOW_INTERACTION_H