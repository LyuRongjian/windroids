#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <android/native_window.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"  {
#endif

// 错误码定义
enum {
    COMPOSITOR_OK = 0,
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
    COMPOSITOR_ERROR_CONFIG_ERROR = -11
};

// 输入事件类型
enum {
    COMPOSITOR_INPUT_NONE = 0,
    COMPOSITOR_INPUT_MOTION = 1,
    COMPOSITOR_INPUT_BUTTON = 2,
    COMPOSITOR_INPUT_KEY = 3,
    COMPOSITOR_INPUT_TOUCH = 4
};

// 输入状态
enum {
    COMPOSITOR_INPUT_STATE_UP = 0,
    COMPOSITOR_INPUT_STATE_DOWN = 1,
    COMPOSITOR_INPUT_STATE_MOVE = 2
};

// 配置参数结构
typedef struct {
    // Xwayland 配置
    int enable_xwayland;           // 是否启用 Xwayland
    const char* xwayland_path;     // Xwayland 可执行文件路径，NULL 则使用默认值
    int xwayland_display_number;   // Xwayland 显示编号
    
    // 渲染配置
    int enable_vsync;              // 是否启用垂直同步
    int preferred_refresh_rate;    // 首选刷新率，0 则使用默认值
    int max_swapchain_images;      // 最大交换链图像数量
    
    // 窗口管理
    int default_window_width;      // 默认窗口宽度
    int default_window_height;     // 默认窗口高度
    int enable_window_decoration;  // 是否启用窗口装饰（边框、标题栏等）
    
    // 调试选项
    int log_level;                 // 日志级别 (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG)
    int enable_tracing;            // 是否启用跟踪
} CompositorConfig;

// 获取默认配置
CompositorConfig* compositor_get_default_config(void);

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

// 获取最后错误码
// 返回: 错误码
int compositor_get_last_error(void);

// 获取错误消息
// - buffer: 输出缓冲区
// - size: 缓冲区大小
// 返回: 错误消息字符串
const char* compositor_get_error_message(char* buffer, size_t size);

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

// 最小化窗口
// - window_title: 窗口标题
// 返回: 0 成功，非 0 失败
int compositor_minimize_window(const char* window_title);

// 最大化窗口
// - window_title: 窗口标题
// 返回: 0 成功，非 0 失败
int compositor_maximize_window(const char* window_title);

// 还原窗口
// - window_title: 窗口标题
// 返回: 0 成功，非 0 失败
int compositor_restore_window(const char* window_title);

// 设置窗口透明度
// - window_title: 窗口标题
// - opacity: 透明度值 (0.0-1.0)
// 返回: 0 成功，非 0 失败
int compositor_set_window_opacity(const char* window_title, float opacity);

// 获取窗口Z顺序
// - window_title: 窗口标题
// 返回: Z顺序值，-1表示失败
int compositor_get_window_z_order(const char* window_title);

// 设置窗口Z顺序
// - window_title: 窗口标题
// - z_order: 新的Z顺序值
// 返回: 0 成功，非 0 失败
int compositor_set_window_z_order(const char* window_title, int z_order);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_H