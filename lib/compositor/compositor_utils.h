#ifndef COMPOSITOR_UTILS_H
#define COMPOSITOR_UTILS_H

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
    COMPOSITOR_ERROR_CONFIG_ERROR = -11,
    COMPOSITOR_ERROR_WINDOW_NOT_FOUND = -12,
    COMPOSITOR_ERROR_UNSUPPORTED_OPERATION = -13,
    COMPOSITOR_ERROR_SYSTEM = -14,
    COMPOSITOR_ERROR_INVALID_STATE = -15,
    COMPOSITOR_ERROR_INVALID_PARAMETER = -16,
    COMPOSITOR_ERROR_RESOURCE_EXHAUSTED = -17,
    COMPOSITOR_ERROR_TIMEOUT = -18,
    COMPOSITOR_ERROR_UNEXPECTED = -19
};

// 全局变量成功状态
#define COMPOSITOR_SUCCESS COMPOSITOR_OK

// 日志级别映射
#define LOG_TAG "WinDroidsCompositor"

// 工具函数声明
static void log_message(int level, const char* format, ...);
static const char* get_error_description(int error_code);
static void set_error(int error_code, const char* format, ...);
static void clear_error(void);
static void mark_dirty_rect(int x, int y, int width, int height);
static void mark_full_redraw(void);

// 公共错误处理函数
int compositor_get_last_error(void);
const char* compositor_get_error_message(char* buffer, size_t size);
const char* compositor_get_error(void);

// 性能相关函数
float compositor_get_fps(void);

#endif // COMPOSITOR_UTILS_H