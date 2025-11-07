#include "compositor_window.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "WindowManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 全局窗口管理器状态
static struct window_manager g_wm = {0};

// 内部函数声明
static void window_remove_from_list(struct window* window);
static void window_add_to_list(struct window* window);
static void window_update_z_order(struct window* window);

// 初始化窗口管理器
int window_manager_init(int screen_width, int screen_height) {
    if (g_wm.windows) {
        LOGE("Window manager already initialized");
        return -1;
    }
    
    memset(&g_wm, 0, sizeof(g_wm));
    g_wm.screen_width = screen_width;
    g_wm.screen_height = screen_height;
    g_wm.next_window_id = 1;
    
    LOGI("Window manager initialized with screen size %dx%d", screen_width, screen_height);
    return 0;
}

// 销毁窗口管理器
void window_manager_destroy(void) {
    // 销毁所有窗口
    struct window* window = g_wm.windows;
    while (window) {
        struct window* next = window->next;
        window_destroy(window);
        window = next;
    }
    
    memset(&g_wm, 0, sizeof(g_wm));
    LOGI("Window manager destroyed");
}

// 创建窗口
struct window* window_create(const struct window_attributes* attrs) {
    if (!attrs) {
        LOGE("Invalid attributes");
        return NULL;
    }
    
    struct window* window = (struct window*)calloc(1, sizeof(struct window));
    if (!window) {
        LOGE("Failed to allocate memory for window");
        return NULL;
    }
    
    // 设置窗口ID
    window->id = g_wm.next_window_id++;
    
    // 复制属性
    window->attrs = *attrs;
    
    // 设置默认值
    if (window->attrs.min_width == 0) window->attrs.min_width = 100;
    if (window->attrs.min_height == 0) window->attrs.min_height = 100;
    if (window->attrs.max_width == 0) window->attrs.max_width = g_wm.screen_width;
    if (window->attrs.max_height == 0) window->attrs.max_height = g_wm.screen_height;
    
    // 确保窗口大小在有效范围内
    if (window->attrs.width < (int)window->attrs.min_width) 
        window->attrs.width = window->attrs.min_width;
    if (window->attrs.height < (int)window->attrs.min_height) 
        window->attrs.height = window->attrs.min_height;
    if (window->attrs.width > (int)window->attrs.max_width) 
        window->attrs.width = window->attrs.max_width;
    if (window->attrs.height > (int)window->attrs.max_height) 
        window->attrs.height = window->attrs.max_height;
    
    // 确保窗口在屏幕内
    if (window->attrs.x < 0) window->attrs.x = 0;
    if (window->attrs.y < 0) window->attrs.y = 0;
    if (window->attrs.x + window->attrs.width > g_wm.screen_width) 
        window->attrs.x = g_wm.screen_width - window->attrs.width;
    if (window->attrs.y + window->attrs.height > g_wm.screen_height) 
        window->attrs.y = g_wm.screen_height - window->attrs.height;
    
    // 添加到窗口列表
    window_add_to_list(window);
    
    // 设置Z轴顺序
    window_update_z_order(window);
    
    LOGI("Created window %d at (%d,%d) size %dx%d", window->id, 
         window->attrs.x, window->attrs.y, window->attrs.width, window->attrs.height);
    
    return window;
}

// 销毁窗口
void window_destroy(struct window* window) {
    if (!window) return;
    
    // 如果是焦点窗口，清除焦点
    if (g_wm.focused_window == window) {
        g_wm.focused_window = NULL;
    }
    
    // 如果是顶层窗口，清除顶层窗口
    if (g_wm.top_window == window) {
        g_wm.top_window = NULL;
    }
    
    // 从列表中移除
    window_remove_from_list(window);
    
    LOGI("Destroyed window %d", window->id);
    free(window);
}

// 设置窗口属性
int window_set_attributes(struct window* window, const struct window_attributes* attrs) {
    if (!window || !attrs) {
        LOGE("Invalid window or attributes");
        return -1;
    }
    
    // 保存旧属性
    struct window_attributes old_attrs = window->attrs;
    
    // 设置新属性
    window->attrs = *attrs;
    
    // 确保窗口大小在有效范围内
    if (window->attrs.width < (int)window->attrs.min_width) 
        window->attrs.width = window->attrs.min_width;
    if (window->attrs.height < (int)window->attrs.min_height) 
        window->attrs.height = window->attrs.min_height;
    if (window->attrs.width > (int)window->attrs.max_width) 
        window->attrs.width = window->attrs.max_width;
    if (window->attrs.height > (int)window->attrs.max_height) 
        window->attrs.height = window->attrs.max_height;
    
    // 确保窗口在屏幕内
    if (window->attrs.x < 0) window->attrs.x = 0;
    if (window->attrs.y < 0) window->attrs.y = 0;
    if (window->attrs.x + window->attrs.width > g_wm.screen_width) 
        window->attrs.x = g_wm.screen_width - window->attrs.width;
    if (window->attrs.y + window->attrs.height > g_wm.screen_height) 
        window->attrs.y = g_wm.screen_height - window->attrs.height;
    
    // 如果位置或大小改变，更新窗口
    if (old_attrs.x != window->attrs.x || old_attrs.y != window->attrs.y ||
        old_attrs.width != window->attrs.width || old_attrs.height != window->attrs.height) {
        // 这里可以添加更新窗口的代码
    }
    
    return 0;
}

// 获取窗口属性
int window_get_attributes(struct window* window, struct window_attributes* attrs) {
    if (!window || !attrs) {
        LOGE("Invalid window or attributes");
        return -1;
    }
    
    *attrs = window->attrs;
    return 0;
}

// 设置窗口位置
int window_set_position(struct window* window, int x, int y) {
    if (!window) {
        LOGE("Invalid window");
        return -1;
    }
    
    // 确保窗口在屏幕内
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + window->attrs.width > g_wm.screen_width) 
        x = g_wm.screen_width - window->attrs.width;
    if (y + window->attrs.height > g_wm.screen_height) 
        y = g_wm.screen_height - window->attrs.height;
    
    window->attrs.x = x;
    window->attrs.y = y;
    
    return 0;
}

// 设置窗口大小
int window_set_size(struct window* window, int width, int height) {
    if (!window) {
        LOGE("Invalid window");
        return -1;
    }
    
    // 确保大小在有效范围内
    if (width < (int)window->attrs.min_width) 
        width = window->attrs.min_width;
    if (height < (int)window->attrs.min_height) 
        height = window->attrs.min_height;
    if (width > (int)window->attrs.max_width) 
        width = window->attrs.max_width;
    if (height > (int)window->attrs.max_height) 
        height = window->attrs.max_height;
    
    window->attrs.width = width;
    window->attrs.height = height;
    
    // 确保窗口在屏幕内
    if (window->attrs.x + window->attrs.width > g_wm.screen_width) 
        window->attrs.x = g_wm.screen_width - window->attrs.width;
    if (window->attrs.y + window->attrs.height > g_wm.screen_height) 
        window->attrs.y = g_wm.screen_height - window->attrs.height;
    
    return 0;
}

// 设置窗口状态
int window_set_state(struct window* window, window_state_t state) {
    if (!window) {
        LOGE("Invalid window");
        return -1;
    }
    
    window_state_t old_state = window->attrs.state;
    window->attrs.state = state;
    
    // 处理状态变化
    switch (state) {
        case WINDOW_STATE_FULLSCREEN:
            // 保存旧位置和大小
            // 设置全屏大小
            window_set_size(window, g_wm.screen_width, g_wm.screen_height);
            window_set_position(window, 0, 0);
            break;
            
        case WINDOW_STATE_MAXIMIZED:
            // 设置最大化大小
            window_set_size(window, g_wm.screen_width, g_wm.screen_height);
            window_set_position(window, 0, 0);
            break;
            
        case WINDOW_STATE_MINIMIZED:
            // 隐藏窗口
            break;
            
        case WINDOW_STATE_NORMAL:
            // 恢复正常大小
            break;
            
        case WINDOW_STATE_HIDDEN:
            // 隐藏窗口
            break;
    }
    
    return 0;
}

// 设置窗口焦点
void window_set_focus(struct window* window) {
    if (!window || !window->attrs.focusable) {
        return;
    }
    
    // 清除之前焦点窗口的焦点
    if (g_wm.focused_window) {
        g_wm.focused_window->has_focus = false;
    }
    
    // 设置新焦点窗口
    g_wm.focused_window = window;
    window->has_focus = true;
    
    // 将焦点窗口移到顶层
    window_raise_to_top(window);
}

// 获取当前焦点窗口
struct window* window_get_focused(void) {
    return g_wm.focused_window;
}

// 将窗口移到顶层
void window_raise_to_top(struct window* window) {
    if (!window) return;
    
    // 从当前位置移除
    window_remove_from_list(window);
    
    // 添加到列表开头
    window->next = g_wm.windows;
    window->prev = NULL;
    
    if (g_wm.windows) {
        g_wm.windows->prev = window;
    }
    
    g_wm.windows = window;
    
    // 更新顶层窗口
    g_wm.top_window = window;
    
    // 更新Z轴顺序
    window_update_z_order(window);
}

// 将窗口移到底层
void window_lower_to_bottom(struct window* window) {
    if (!window) return;
    
    // 从当前位置移除
    window_remove_from_list(window);
    
    // 找到最后一个窗口
    struct window* last = g_wm.windows;
    while (last && last->next) {
        last = last->next;
    }
    
    // 添加到列表末尾
    if (last) {
        last->next = window;
        window->prev = last;
        window->next = NULL;
    } else {
        // 列表为空
        g_wm.windows = window;
        window->prev = NULL;
        window->next = NULL;
    }
    
    // 更新Z轴顺序
    window_update_z_order(window);
}

// 查找指定点下的窗口
struct window* window_find_at_point(int x, int y) {
    struct window* window = g_wm.windows;
    
    // 从顶层窗口开始查找
    while (window) {
        if (window->attrs.state != WINDOW_STATE_HIDDEN && 
            window->attrs.state != WINDOW_STATE_MINIMIZED) {
            if (x >= window->attrs.x && x < window->attrs.x + window->attrs.width &&
                y >= window->attrs.y && y < window->attrs.y + window->attrs.height) {
                return window;
            }
        }
        window = window->next;
    }
    
    return NULL;
}

// 处理窗口移动
int window_handle_move(struct window* window, int dx, int dy) {
    if (!window || !window->attrs.movable) {
        return -1;
    }
    
    return window_set_position(window, window->attrs.x + dx, window->attrs.y + dy);
}

// 处理窗口大小调整
int window_handle_resize(struct window* window, int width, int height) {
    if (!window || !window->attrs.resizable) {
        return -1;
    }
    
    return window_set_size(window, width, height);
}

// 更新窗口管理器（每帧调用）
void window_manager_update(void) {
    // 这里可以添加窗口管理器的更新逻辑
    // 例如动画、状态更新等
}

// 内部函数：从列表中移除窗口
static void window_remove_from_list(struct window* window) {
    if (!window) return;
    
    if (window->prev) {
        window->prev->next = window->next;
    } else {
        // 是第一个窗口
        g_wm.windows = window->next;
    }
    
    if (window->next) {
        window->next->prev = window->prev;
    }
    
    window->prev = NULL;
    window->next = NULL;
}

// 内部函数：添加窗口到列表
static void window_add_to_list(struct window* window) {
    if (!window) return;
    
    // 添加到列表开头
    window->next = g_wm.windows;
    window->prev = NULL;
    
    if (g_wm.windows) {
        g_wm.windows->prev = window;
    }
    
    g_wm.windows = window;
    
    // 更新顶层窗口
    g_wm.top_window = window;
}

// 内部函数：更新窗口Z轴顺序
static void window_update_z_order(struct window* window) {
    if (!window) return;
    
    // 计算Z轴顺序
    uint32_t z_order = 0;
    struct window* w = g_wm.windows;
    while (w) {
        if (w == window) {
            window->z_order = z_order;
            break;
        }
        z_order++;
        w = w->next;
    }
}