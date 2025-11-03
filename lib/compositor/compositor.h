#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <android/native_window.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/epoll.h>

#ifdef __cplusplus
extern "C"  {
#endif

// 错误码定义
enum {
    COMPOSITOR_SUCCESS = 0,
    COMPOSITOR_ERROR_INIT = -1,
    COMPOSITOR_ERROR_VULKAN = -2,
    COMPOSITOR_ERROR_XWAYLAND = -3,
    COMPOSITOR_ERROR_WLROOTS = -4,
    COMPOSITOR_ERROR_MEMORY = -5,
    COMPOSITOR_ERROR_INVALID_ARGS = -6,
    COMPOSITOR_ERROR_NOT_INITIALIZED = -7,
    COMPOSITOR_ERROR_SURFACE_ERROR = -8,
    COMPOSITOR_ERROR_INPUT_DEVICE_ERROR = -9,
    COMPOSITOR_ERROR_SWAPCHAIN_ERROR = -10,
    COMPOSITOR_ERROR_CONFIG_ERROR = -11,
    COMPOSITOR_ERROR_WINDOW_NOT_FOUND = -12,
    COMPOSITOR_ERROR_UNSUPPORTED_OPERATION = -13,
    COMPOSITOR_ERROR_SYSTEM = -14,
    COMPOSITOR_ERROR_INVALID_STATE = -15,
    COMPOSITOR_ERROR_INVALID_PARAMETER = -16,
    COMPOSITOR_ERROR_RESOURCE_EXHAUSTED = -17,
    COMPOSITOR_ERROR_TIMEOUT = -18,
    COMPOSITOR_ERROR_UNEXPECTED = -19  // 与代码中使用的错误码保持一致
};

// 输入事件类型
typedef enum {
    INPUT_EVENT_MOUSE_MOTION,
    INPUT_EVENT_MOUSE_BUTTON,
    INPUT_EVENT_KEYBOARD,
    INPUT_EVENT_TOUCH,
    INPUT_EVENT_SCROLL,
    INPUT_EVENT_GESTURE,
} InputEventType;

// 输入事件结构体
typedef struct {
    InputEventType type;
    union {
        struct {
            int x, y;
            int delta_x, delta_y;
        } motion;
        struct {
            int x, y;
            int button;
            bool pressed;
        } mouse_button;
        struct {
            int key_code;
            bool pressed;
            int modifiers;
        } keyboard;
        struct {
            int x, y;
            int action;
            int pointer_id;
            float pressure;
        } touch;
        struct {
            float delta_x, delta_y;
            int fingers;
        } scroll;
    } data;
} InputEvent;

// 输入事件状态
typedef enum {
    COMPOSITOR_INPUT_STATE_UP,
    COMPOSITOR_INPUT_STATE_DOWN,
    COMPOSITOR_INPUT_STATE_MOVE
} CompositorInputState;

// 窗口状态枚举
typedef enum {
    WINDOW_STATE_NORMAL,
    WINDOW_STATE_MINIMIZED,
    WINDOW_STATE_MAXIMIZED,
    WINDOW_STATE_FULLSCREEN
} WindowState;

// 窗口常量定义
typedef enum {
    WINDOW_TITLEBAR_HEIGHT = 30,      // 窗口标题栏高度
    WINDOW_BORDER_WIDTH = 1,          // 窗口边框宽度
    WINDOW_SNAP_DISTANCE = 10,        // 默认窗口吸附距离
    DEFAULT_Z_ORDER_INCREMENT = 1     // Z顺序默认增量
} WindowConstants;

// 配置参数结构
typedef struct {
    // Xwayland 配置
    bool enable_xwayland;          // 是否启用 Xwayland
    char xwayland_path[256];       // Xwayland 可执行文件路径，使用固定大小数组更安全
    int xwayland_display_number;   // Xwayland 显示编号
    
    // 渲染配置
    bool enable_vsync;             // 是否启用垂直同步
    int preferred_refresh_rate;    // 首选刷新率，0 则使用默认值
    int max_swapchain_images;      // 最大交换链图像数量
    float initial_scale;           // 初始缩放比例
    
    // 窗口管理
    int default_window_width;      // 默认窗口宽度
    int default_window_height;     // 默认窗口高度
    bool enable_window_decoration; // 是否启用窗口装饰（边框、标题栏等）
    bool enable_window_snapping;   // 启用窗口边缘吸附
    int window_snap_distance;      // 窗口吸附距离
    
    // 调试选项
    int log_level;                 // 日志级别 (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG)
    bool enable_tracing;           // 是否启用跟踪
    bool enable_perf_monitoring;   // 是否启用性能监控
    
    // 其他配置
    float background_color[3];     // 背景颜色 RGB
    bool enable_debug_logging;     // 调试日志标志
} CompositorConfig;

// 获取默认配置
CompositorConfig* compositor_get_default_config(void);

// 设置日志级别
void compositor_set_log_level(int level);

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

// 注入输入事件（触摸、键盘等）
// - type: 事件类型（COMPOSITOR_INPUT_*）
// - x/y: 坐标位置
// - key/button: 键码或按钮
// - state: 状态（COMPOSITOR_INPUT_STATE_*）
void compositor_handle_input(int type, int x, int y, int key, int state);

// 发送输入事件到合成器
int compositor_send_input_event(const InputEvent* event);

// 获取最后错误码
// 返回: 错误码
int compositor_get_last_error(void);

// 获取错误消息
// - buffer: 输出缓冲区，可为NULL
// - size: 缓冲区大小，为0时忽略
// 返回: 错误消息字符串，如果buffer为NULL则返回内部缓冲区（线程不安全）
const char* compositor_get_error_message(char* buffer, size_t size);

// 获取最近的错误信息
const char* compositor_get_error(void);

// 获取当前帧率
// 返回: 平均帧率
float compositor_get_fps(void);

// 设置窗口大小
// - width/height: 新的窗口大小
// 返回: 0 成功，非 0 失败
int compositor_resize(int width, int height);

// 获取活动窗口标题
// 返回: 当前活动窗口的标题，如果没有活动窗口则返回NULL
const char* compositor_get_active_window_title();

// 设置窗口焦点
// - window_title: 窗口标题
// 返回: 0 成功，非 0 失败
int compositor_set_window_focus(const char* window_title);

// 移动窗口到最前
// - window_title: 窗口标题
// 返回: 0 成功，非 0 失败
int compositor_activate_window(const char* window_title);

// 获取窗口信息（位置和大小）
// - window_title: 窗口标题
// - x/y: 输出参数，窗口位置坐标
// - width/height: 输出参数，窗口大小
// 返回: 0 成功，非 0 失败
int compositor_get_window_info(const char* window_title, int* x, int* y, int* width, int* height);

// 获取窗口列表
// - window_titles: 输出参数，窗口标题数组
// - count: 输出参数，窗口数量
// 返回: 0 成功，非 0 失败
int compositor_get_window_list(char*** window_titles, int* count);

// 释放窗口列表
// - window_titles: 窗口标题数组
// - count: 窗口数量
void compositor_free_window_list(char*** window_titles, int count);

// 关闭指定窗口
// - window_title: 窗口标题
// 返回: 0 成功，非 0 失败
int compositor_close_window(const char* window_title);

// 调整窗口大小
// - window_title: 窗口标题
// - width/height: 新的窗口大小
// 返回: 0 成功，非 0 失败
int compositor_resize_window(const char* window_title, int width, int height);

// 移动窗口位置
// - window_title: 窗口标题
// - x/y: 新的窗口位置坐标
// 返回: 0 成功，非 0 失败
int compositor_move_window(const char* window_title, int x, int y);

// 窗口操作函数
int compositor_minimize_window(const char* window_title);
int compositor_maximize_window(const char* window_title);
int compositor_restore_window(const char* window_title);
int compositor_set_window_opacity(const char* window_title, float opacity);
int compositor_get_window_z_order(const char* window_title);
int compositor_set_window_z_order(const char* window_title, int z_order);

// 检查点是否在Xwayland窗口内
// - surface: Xwayland窗口表面
// - x/y: 坐标点
// 返回: 如果点在窗口内返回true，否则返回false
static bool is_point_in_xwayland_surface(struct wlr_xwayland_surface *surface, int x, int y);

// 窗口状态结构体
typedef struct {
    WindowState state;                // 当前窗口状态
    int saved_x, saved_y;             // 保存的位置
    int saved_width, saved_height;    // 保存的大小
    bool is_fullscreen;               // 是否全屏
} WindowSavedState;

// 检查点是否在窗口内
// - surface: 窗口表面
// - x/y: 坐标点
// 返回: 如果点在窗口内返回true，否则返回false
bool is_point_in_window(void* surface, int x, int y);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_H