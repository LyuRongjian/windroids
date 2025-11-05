#ifndef COMPOSITOR_WINDOW_H
#define COMPOSITOR_WINDOW_H

#include <stdint.h>
#include <stdbool.h>
#include "compositor.h" // 包含基础定义

// 窗口状态枚举（保持与compositor.h一致）
#ifndef WINDOW_STATE_NORMAL
#define WINDOW_STATE_NORMAL 0
#define WINDOW_STATE_MINIMIZED 1
#define WINDOW_STATE_MAXIMIZED 2
#define WINDOW_STATE_FULLSCREEN 3
#endif

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

// 工作区/虚拟桌面
typedef struct {
    char* name;
    bool is_active;
    int32_t window_count;
    void** windows;               // 窗口指针数组
    bool* is_wayland;             // 对应的窗口类型
    WindowGroup* window_groups;   // 窗口组数组
    int32_t group_count;          // 窗口组数量
} Workspace;

// 窗口组
typedef struct {
    char* name;
    int32_t window_count;
    void** windows;               // 窗口指针数组
    bool* is_wayland;             // 对应的窗口类型
} WindowGroup;

// Xwayland窗口状态
typedef struct XwaylandWindowState {
    int32_t x, y;
    int32_t width, height;
    WindowState state;
    bool focused;
    const char* title;
    uint32_t window_id;
    void* surface;
    float opacity;
    int32_t z_order;           // Z轴顺序
    void* render_data;         // 渲染相关数据
    bool is_dirty;             // 脏标记
    DirtyRect* dirty_regions;  // 脏区域列表
    int32_t dirty_region_count;// 脏区域数量
    
    // 多窗口管理增强
    int32_t workspace_id;      // 所属工作区ID
    int32_t group_id;          // 所属窗口组ID
    bool is_fullscreen;        // 是否全屏
    bool is_maximized;         // 是否最大化
    bool is_minimized;         // 是否最小化
    bool is_shaded;            // 是否最小化到标题栏
    bool is_sticky;            // 是否在所有工作区显示
    
    // 窗口装饰和特效
    bool has_shadow;           // 是否有阴影
    bool has_border;           // 是否有边框
    float shadow_opacity;      // 阴影透明度
    int32_t shadow_size;       // 阴影大小
    
    // 动画状态
    bool is_animating;         // 是否正在动画中
    float animation_progress;  // 动画进度
    int animation_type;        // 动画类型
    
    // 保存的窗口状态（用于恢复）
    int32_t saved_x, saved_y;
    int32_t saved_width, saved_height;
    WindowState saved_state;
    
    // wlroots相关字段（如果使用）
    struct wl_list link;
    struct wlr_xwayland_surface *wlr_surface;
} XwaylandWindowState;

// Wayland窗口
typedef struct WaylandWindow {
    int32_t x, y;
    int32_t width, height;
    WindowState state;
    bool focused;
    const char* title;
    uint32_t window_id;
    void* surface;
    float opacity;
    int32_t z_order;           // Z轴顺序
    void* render_data;         // 渲染相关数据
    bool is_dirty;             // 脏标记
    DirtyRect* dirty_regions;  // 脏区域列表
    int32_t dirty_region_count;// 脏区域数量
    
    // 多窗口管理增强
    int32_t workspace_id;      // 所属工作区ID
    int32_t group_id;          // 所属窗口组ID
    bool is_fullscreen;        // 是否全屏
    bool is_maximized;         // 是否最大化
    bool is_minimized;         // 是否最小化
    bool is_shaded;            // 是否最小化到标题栏
    bool is_sticky;            // 是否在所有工作区显示
    
    // 窗口装饰和特效
    bool has_shadow;           // 是否有阴影
    bool has_border;           // 是否有边框
    float shadow_opacity;      // 阴影透明度
    int32_t shadow_size;       // 阴影大小
    
    // 动画状态
    bool is_animating;         // 是否正在动画中
    float animation_progress;  // 动画进度
    int animation_type;        // 动画类型
    
    // 保存的窗口状态（用于恢复）
    int32_t saved_x, saved_y;
    int32_t saved_width, saved_height;
    WindowState saved_state;
    
    // wlroots相关字段（如果使用）
    struct wl_list link;
    struct wlr_surface *wlr_surface;
    bool mapped;
} WaylandWindow;

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

// 添加Xwayland窗口函数（内部使用）
int add_xwayland_window(XwaylandWindow* window);

// 添加Wayland窗口函数（内部使用）
int add_wayland_window(WaylandWindow* window);

// 窗口排序函数 - 根据Z顺序排序窗口
void compositor_sort_windows_by_z_order(CompositorState* state);

// 清理所有窗口
void cleanup_windows(CompositorState* state);

// 查找窗口函数（根据标题）
void* find_window_by_title(const char* title, bool* out_is_wayland);

#endif // COMPOSITOR_WINDOW_H