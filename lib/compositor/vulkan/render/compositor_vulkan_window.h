#ifndef COMPOSITOR_VULKAN_WINDOW_H
#define COMPOSITOR_VULKAN_WINDOW_H

#include "compositor_vulkan.h"

// 窗口信息结构体
typedef struct {
    const char* title;
    int x, y;
    int width, height;
    WindowState state;
    float opacity;
    int z_order;
    bool is_wayland;
} WindowInfo;

// 窗口渲染相关函数声明
void render_windows(CompositorState* state);
void render_window(CompositorState* state, WindowInfo* window, bool is_wayland);
void render_windows_with_hardware_acceleration(CompositorState* state);
int prepare_window_rendering(CompositorState* state, void* window, bool is_wayland);
void finish_window_rendering(CompositorState* state, void* window, bool is_wayland);

// 窗口辅助函数声明
bool check_window_intersects_dirty_rect(CompositorState* state, void* window, bool is_wayland);
bool is_window_completely_occluded(void* front_window, bool front_is_wayland, void* back_window, bool back_is_wayland);
void batch_windows_by_texture(CompositorState* state, void** windows, bool* window_types, int count);

#endif // COMPOSITOR_VULKAN_WINDOW_H