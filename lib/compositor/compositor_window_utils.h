#ifndef COMPOSITOR_WINDOW_UTILS_H
#define COMPOSITOR_WINDOW_UTILS_H

#include "compositor.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 窗口查找结果结构体
typedef struct {
    void* window;
    bool is_wayland;
    int z_order;
} WindowSearchResult;

// 通用窗口查找函数
WindowSearchResult compositor_find_window_by_title(const char* title);
WindowSearchResult compositor_find_window_at_position(int x, int y);
WindowSearchResult compositor_find_active_window(void);

// 窗口迭代函数 - 对每个窗口执行回调
typedef bool (*WindowCallback)(void* window, bool is_wayland, void* user_data);
void compositor_for_each_window(WindowCallback callback, void* user_data);

// 窗口排序辅助函数
int compositor_compare_window_z_order(const void* a, const void* b);

// 窗口状态检查函数
bool compositor_is_window_minimized(void* window, bool is_wayland);
bool compositor_is_window_maximized(void* window, bool is_wayland);
bool compositor_is_window_visible(void* window, bool is_wayland);

// 窗口位置和大小获取函数
void compositor_get_window_geometry(void* window, bool is_wayland, int* x, int* y, int* width, int* height);
const char* compositor_get_window_title(void* window, bool is_wayland);

// 窗口操作辅助函数
void compositor_mark_window_dirty(void* window, bool is_wayland);
void compositor_update_window_z_order(void* window, bool is_wayland, int z_order);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_WINDOW_UTILS_H