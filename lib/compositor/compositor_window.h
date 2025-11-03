#ifndef COMPOSITOR_WINDOW_H
#define COMPOSITOR_WINDOW_H

#include <stdint.h>

// 窗口状态枚举
enum {
    WINDOW_STATE_NORMAL = 0,      // 普通状态
    WINDOW_STATE_MINIMIZED = 1,   // 最小化状态
    WINDOW_STATE_MAXIMIZED = 2,   // 最大化状态
    WINDOW_STATE_FULLSCREEN = 3   // 全屏状态
};

// 窗口常量定义
#define WINDOW_TITLEBAR_HEIGHT 32    // 窗口标题栏高度
#define WINDOW_BORDER_WIDTH 1        // 窗口边框宽度
#define WINDOW_CORNER_RADIUS 8       // 窗口圆角半径
#define WINDOW_MARGIN 8              // 窗口边缘间距
#define WINDOW_MIN_WIDTH 100         // 窗口最小宽度
#define WINDOW_MIN_HEIGHT 100        // 窗口最小高度
#define WINDOW_MINIMIZED_Y -10000    // 窗口最小化时的Y坐标
#define WINDOW_DEFAULT_OPACITY 1.0f  // 窗口默认透明度

// 窗口保存状态结构体
typedef struct {
    int state;                    // 窗口状态 (WINDOW_STATE_*)
    int saved_x;                  // 保存的X坐标
    int saved_y;                  // 保存的Y坐标
    int saved_width;              // 保存的宽度
    int saved_height;             // 保存的高度
    bool is_fullscreen;           // 是否为全屏状态
    float saved_opacity;          // 保存的透明度
} WindowSavedState;

// 窗口信息结构体
typedef struct {
    const char* title;            // 窗口标题
    int x, y;                     // 窗口位置
    int width, height;            // 窗口尺寸
    int state;                    // 窗口状态
    float opacity;                // 窗口透明度
    int z_order;                  // 窗口Z顺序
    bool is_wayland;              // 是否为Wayland窗口
} WindowInfo;

// Wayland窗口结构体定义
typedef struct {
    struct wl_list link;
    struct wlr_surface *surface;
    char *title;
    int x, y;
    int width, height;
    bool mapped;
    bool minimized;
    bool maximized;
    float opacity;
    int z_order;
} WaylandWindow;

// Xwayland窗口状态结构体
typedef struct {
    struct wl_list link;
    struct wlr_xwayland_surface *surface;
    WindowSavedState state;
    int z_order;
} XwaylandWindowState;

// Wayland窗口状态结构体
typedef struct {
    struct wl_list link;
    struct WaylandWindow *window;
    WindowSavedState state;
    int z_order;
} WaylandWindowState;

// 窗口管理相关函数声明
int compositor_activate_window(const char* window_title);
int compositor_close_window(const char* window_title);
int compositor_resize_window(const char* window_title, int width, int height);
int compositor_move_window(const char* window_title, int x, int y);
int compositor_minimize_window(const char* window_title);
int compositor_maximize_window(const char* window_title);
int compositor_restore_window(const char* window_title);
int compositor_set_window_opacity(const char* window_title, float opacity);
int compositor_get_window_z_order(const char* window_title);
int compositor_set_window_z_order(const char* window_title, int z_order);
int compositor_get_window_info(const char* window_title, WindowInfo* info);
int compositor_get_all_windows(int* count, char*** titles);
int compositor_get_all_windows_info(WindowInfo** windows, int* count);
int compositor_get_window_info_by_ptr(void* window_ptr, bool is_wayland_window, WindowInfo* info);
int compositor_get_active_window_info(WindowInfo* info);
int compositor_get_window_count(bool include_wayland, bool include_xwayland);

#endif // COMPOSITOR_WINDOW_H