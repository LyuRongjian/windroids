#ifndef COMPOSITOR_WINDOW_H
#define COMPOSITOR_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 窗口类型
typedef enum {
    WINDOW_TYPE_NORMAL,    // 普通窗口
    WINDOW_TYPE_POPUP,     // 弹出窗口
    WINDOW_TYPE_TOOLTIP,   // 工具提示
    WINDOW_TYPE_MENU       // 菜单
} window_type_t;

// 窗口状态
typedef enum {
    WINDOW_STATE_NORMAL,   // 正常状态
    WINDOW_STATE_MAXIMIZED,// 最大化
    WINDOW_STATE_MINIMIZED,// 最小化
    WINDOW_STATE_FULLSCREEN,// 全屏
    WINDOW_STATE_HIDDEN    // 隐藏
} window_state_t;

// 窗口属性
struct window_attributes {
    int x, y;              // 窗口位置
    int width, height;     // 窗口大小
    window_type_t type;    // 窗口类型
    window_state_t state;  // 窗口状态
    bool resizable;        // 是否可调整大小
    bool movable;          // 是否可移动
    bool focusable;        // 是否可获得焦点
    uint32_t min_width;    // 最小宽度
    uint32_t min_height;   // 最小高度
    uint32_t max_width;    // 最大宽度
    uint32_t max_height;   // 最大高度
};

// 窗口结构
struct window {
    uint32_t id;                           // 窗口ID
    struct window_attributes attrs;        // 窗口属性
    bool has_focus;                        // 是否有焦点
    uint32_t z_order;                      // Z轴顺序
    void* user_data;                       // 用户数据
    struct window* parent;                 // 父窗口
    struct window* next;                    // 下一个窗口（链表）
    struct window* prev;                    // 上一个窗口（链表）
};

// 窗口管理器状态
struct window_manager {
    struct window* windows;                // 窗口链表
    struct window* focused_window;         // 当前焦点窗口
    struct window* top_window;             // 顶层窗口
    uint32_t next_window_id;               // 下一个窗口ID
    int screen_width, screen_height;       // 屏幕尺寸
};

// 初始化窗口管理器
int window_manager_init(int screen_width, int screen_height);

// 销毁窗口管理器
void window_manager_destroy(void);

// 创建窗口
struct window* window_create(const struct window_attributes* attrs);

// 销毁窗口
void window_destroy(struct window* window);

// 设置窗口属性
int window_set_attributes(struct window* window, const struct window_attributes* attrs);

// 获取窗口属性
int window_get_attributes(struct window* window, struct window_attributes* attrs);

// 设置窗口位置
int window_set_position(struct window* window, int x, int y);

// 设置窗口大小
int window_set_size(struct window* window, int width, int height);

// 设置窗口状态
int window_set_state(struct window* window, window_state_t state);

// 设置窗口焦点
void window_set_focus(struct window* window);

// 获取当前焦点窗口
struct window* window_get_focused(void);

// 将窗口移到顶层
void window_raise_to_top(struct window* window);

// 将窗口移到底层
void window_lower_to_bottom(struct window* window);

// 查找指定点下的窗口
struct window* window_find_at_point(int x, int y);

// 处理窗口移动
int window_handle_move(struct window* window, int dx, int dy);

// 处理窗口大小调整
int window_handle_resize(struct window* window, int width, int height);

// 更新窗口管理器（每帧调用）
void window_manager_update(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_WINDOW_H