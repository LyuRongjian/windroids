#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <android/native_window.h>
#include <stdint.h>
#include <stdbool.h>

// 窗口状态枚举
typedef enum {
    WINDOW_STATE_NORMAL = 0,
    WINDOW_STATE_MINIMIZED,
    WINDOW_STATE_MAXIMIZED,
    WINDOW_STATE_FULLSCREEN
} WindowState;

// 脏区域结构
typedef struct {
    int32_t x, y;
    int32_t width, height;
} DirtyRect;

// Xwayland窗口状态
typedef struct {
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
} XwaylandWindowState;

// Wayland窗口
typedef struct {
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
} WaylandWindow;

// Xwayland窗口管理状态
typedef struct {
    XwaylandWindowState** windows;
    int32_t window_count;
    int32_t max_windows;
    int32_t capacity;          // 实际分配的容量（支持动态扩容）
} XwaylandState;

// Wayland窗口管理状态
typedef struct {
    WaylandWindow** windows;
    int32_t window_count;
    int32_t max_windows;
    int32_t capacity;          // 实际分配的容量（支持动态扩容）
} WaylandState;

// 工作区/虚拟桌面
typedef struct {
    char* name;
    bool is_active;
    int32_t window_count;
    void** windows;           // 窗口指针数组
    bool* is_wayland;         // 对应的窗口类型
    WindowGroup* window_groups; // 窗口组数组
    int32_t group_count;      // 窗口组数量
} Workspace;

// 窗口组
typedef struct {
    char* name;
    int32_t window_count;
    void** windows;           // 窗口指针数组
    bool* is_wayland;         // 对应的窗口类型
} WindowGroup;

// 窗口平铺模式
enum {
    TILE_MODE_NONE = 0,       // 无平铺
    TILE_MODE_HORIZONTAL = 1, // 水平平铺
    TILE_MODE_VERTICAL = 2,   // 垂直平铺
    TILE_MODE_GRID = 3        // 网格平铺
};

// 合成器状态
typedef struct CompositorState {
    // 窗口和显示
    ANativeWindow* window;
    int32_t width;
    int32_t height;
    bool needs_redraw;
    
    // 配置
    CompositorConfig config;
    
    // 窗口管理
    XwaylandState xwayland_state;
    WaylandState wayland_state;
    void* active_window;
    bool active_window_is_wayland;
    int32_t next_z_order;      // 下一个窗口的Z轴顺序
    
    // 多窗口管理增强
    Workspace* workspaces;
    int32_t workspace_count;
    int32_t active_workspace;
    WindowGroup* window_groups;
    int32_t window_group_count;
    int32_t tile_mode;         // 窗口平铺模式
    
    // 窗口预览和快照
    void** window_snapshots;
    bool* snapshot_is_wayland;
    int32_t snapshot_count;
    
    // 渲染和输入状态
    void* vulkan_state;
    void* input_state;
    
    // 性能统计
    int64_t last_frame_time;
    float fps;
    int64_t frame_count;       // 总帧数
    int64_t total_render_time; // 总渲染时间
    float avg_frame_time;      // 平均帧时间
    
    // 内存管理
    size_t total_allocated;    // 总分配内存
    size_t peak_allocated;     // 峰值内存使用
    
    // 渲染优化
    DirtyRect* dirty_rects;    // 全局脏区域
    int32_t dirty_rect_count;  // 脏区域数量
    bool use_dirty_rect_optimization; // 是否启用脏区域优化
    
    // 错误状态
    int last_error;
    char error_message[256];
} CompositorState;

// 包含拆分后的头文件
#include "compositor_config.h"
#include "compositor_window.h"
#include "compositor_input.h"
#include "compositor_vulkan.h"
#include "compositor_utils.h"

#ifdef __cplusplus
extern "C"  {
#endif

// 初始化合成器
// - window: 来自 GameActivity.app->window
// - width/height: 建议从 ANativeWindow_getWidth/Height 获取
// - config: 配置参数，NULL 则使用默认配置
int compositor_init(ANativeWindow* window, int width, int height, CompositorConfig* config);

// 主循环单步（应在 GameActivity 的渲染线程中循环调用）
// 返回 0 表示正常，非 0 表示错误
int compositor_step(void);

// 销毁合成器
void compositor_destroy(void);

// 设置窗口大小
// - width/height: 新的窗口大小
// 返回: 0 成功，非 0 失败
int compositor_resize(int width, int height);

// 获取活动窗口标题
// 返回: 当前活动窗口的标题，如果没有活动窗口则返回NULL
const char* compositor_get_active_window_title();

// 触发重绘
void compositor_schedule_redraw(void);

// 标记脏区域
void compositor_mark_dirty_rect(int x, int y, int width, int height);

// 清除脏区域
void compositor_clear_dirty_rects(void);

// 内部使用函数，获取当前状态
CompositorState* compositor_get_state(void);

// 检查是否已初始化
bool compositor_is_initialized(void);

// 查找位置处的表面
void* find_surface_at_position(int x, int y, bool* out_is_wayland);

// 按Z顺序排序窗口
void compositor_sort_windows_by_z_order(void);

// 获取窗口Z顺序
int compositor_get_window_z_order(const char* window_title);

// 设置窗口Z顺序
int compositor_set_window_z_order(const char* window_title, int z_order);

// 多窗口管理增强函数
int compositor_create_workspace(const char* name);
int compositor_switch_workspace(int workspace_index);
int compositor_move_window_to_workspace(const char* window_title, int workspace_index);
int compositor_group_windows(const char** window_titles, int count, const char* group_name);
int compositor_ungroup_windows(const char* group_name);
int compositor_tile_windows(int tile_mode);
int compositor_cascade_windows();
int compositor_take_window_snapshot(const char* window_title);
int compositor_show_window_previews();
int compositor_close_all_windows();
int compositor_minimize_all_windows();
int compositor_restore_all_windows();

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_H