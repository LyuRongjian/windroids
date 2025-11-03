#include "compositor_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 全局错误状态
static int g_last_error = COMPOSITOR_OK;
static char g_error_message[1024] = {0};

// 日志级别映射
static const char* log_level_names[] = {
    "ERROR",
    "WARN", 
    "INFO",
    "DEBUG"
};

// 当前日志级别
static int g_current_log_level = COMPOSITOR_LOG_INFO;

// 性能统计数据
static float g_fps = 0.0f;
static float g_frame_times[60] = {0.0f};
static int g_frame_time_index = 0;
static clock_t g_last_frame_time = 0;

// 日志输出函数
void log_message(int level, const char* format, ...) {
    if (level > g_current_log_level) {
        return; // 低于当前日志级别的消息不输出
    }
    
    // 获取时间戳
    time_t now = time(NULL);
    struct tm* local_time = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local_time);
    
    // 输出日志头
    fprintf(stderr, "[%s] [%s] ", time_str, log_level_names[level]);
    
    // 输出实际消息
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
    fflush(stderr);
}

// 设置错误信息
void set_error(int error_code, const char* format, ...) {
    g_last_error = error_code;
    
    // 构建错误消息
    va_list args;
    va_start(args, format);
    vsnprintf(g_error_message, sizeof(g_error_message), format, args);
    va_end(args);
    
    // 记录错误日志
    log_message(COMPOSITOR_LOG_ERROR, "Error %d: %s", error_code, g_error_message);
}

// 获取最后错误码
int compositor_get_last_error(void) {
    return g_last_error;
}

// 获取错误消息
const char* compositor_get_error_message(char* buffer, size_t size) {
    if (buffer && size > 0) {
        strncpy(buffer, g_error_message, size - 1);
        buffer[size - 1] = '\0';
        return buffer;
    }
    return g_error_message;
}

// 获取错误描述
const char* get_error_description(int error_code) {
    switch (error_code) {
        case COMPOSITOR_OK: return "Success";
        case COMPOSITOR_ERROR_INIT: return "Initialization failed";
        case COMPOSITOR_ERROR_VULKAN: return "Vulkan error";
        case COMPOSITOR_ERROR_XWAYLAND: return "Xwayland error";
        case COMPOSITOR_ERROR_WLROOTS: return "wlroots error";
        case COMPOSITOR_ERROR_MEMORY: return "Memory allocation failed";
        case COMPOSITOR_ERROR_INVALID_ARGS: return "Invalid arguments";
        case COMPOSITOR_ERROR_NOT_INITIALIZED: return "Compositor not initialized";
        case COMPOSITOR_ERROR_SURFACE_ERROR: return "Surface error";
        case COMPOSITOR_ERROR_INPUT_DEVICE_ERROR: return "Input device error";
        case COMPOSITOR_ERROR_SWAPCHAIN_ERROR: return "Swapchain error";
        case COMPOSITOR_ERROR_CONFIG_ERROR: return "Configuration error";
        case COMPOSITOR_ERROR_WINDOW_NOT_FOUND: return "Window not found";
        case COMPOSITOR_ERROR_UNSUPPORTED_OPERATION: return "Unsupported operation";
        case COMPOSITOR_ERROR_SYSTEM: return "System error";
        case COMPOSITOR_ERROR_INVALID_STATE: return "Invalid state";
        case COMPOSITOR_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case COMPOSITOR_ERROR_RESOURCE_EXHAUSTED: return "Resource exhausted";
        case COMPOSITOR_ERROR_TIMEOUT: return "Operation timed out";
        case COMPOSITOR_ERROR_UNEXPECTED: return "Unexpected error";
        default: return "Unknown error";
    }
}

// 设置日志级别
void utils_set_log_level(int level) {
    if (level >= COMPOSITOR_LOG_ERROR && level <= COMPOSITOR_LOG_DEBUG) {
        g_current_log_level = level;
        log_message(COMPOSITOR_LOG_INFO, "Log level changed to %s", log_level_names[level]);
    } else {
        log_message(COMPOSITOR_LOG_WARN, "Invalid log level: %d, using current level", level);
    }
}

// 更新性能统计
void update_performance_stats(void) {
    clock_t current_time = clock();
    if (g_last_frame_time > 0) {
        float frame_time = (float)(current_time - g_last_frame_time) / CLOCKS_PER_SEC;
        
        // 保存帧时间
        g_frame_times[g_frame_time_index] = frame_time;
        g_frame_time_index = (g_frame_time_index + 1) % 60;
        
        // 计算平均FPS
        float total_time = 0.0f;
        int valid_frames = 0;
        
        for (int i = 0; i < 60; i++) {
            if (g_frame_times[i] > 0) {
                total_time += g_frame_times[i];
                valid_frames++;
            }
        }
        
        if (valid_frames > 0) {
            float avg_frame_time = total_time / valid_frames;
            g_fps = 1.0f / avg_frame_time;
        }
    }
    
    g_last_frame_time = current_time;
}

// 获取当前FPS
float compositor_get_fps(void) {
    return g_fps;
}

// 标记脏区域
void mark_dirty_rect(CompositorState* state, int x, int y, int width, int height) {
    if (!state) return;
    
    // 简单实现：标记整个屏幕为脏
    // 实际应用中可以维护脏区域列表以提高性能
    state->needs_redraw = true;
    
    // 如果需要更精确的脏区域管理，可以在这里实现
    // 例如合并重叠区域、维护脏矩形列表等
}

// 检查两个矩形是否相交
bool rects_intersect(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    return !(x1 >= x2 + w2 || x1 + w1 <= x2 || y1 >= y2 + h2 || y1 + h1 <= y2);
}

// 计算矩形交集
bool calculate_intersection(int x1, int y1, int w1, int h1, 
                           int x2, int y2, int w2, int h2,
                           int* out_x, int* out_y, int* out_w, int* out_h) {
    if (!rects_intersect(x1, y1, w1, h1, x2, y2, w2, h2)) {
        return false;
    }
    
    *out_x = (x1 > x2) ? x1 : x2;
    *out_y = (y1 > y2) ? y1 : y2;
    *out_w = ((x1 + w1 < x2 + w2) ? (x1 + w1) : (x2 + w2)) - *out_x;
    *out_h = ((y1 + h1 < y2 + h2) ? (y1 + h1) : (y2 + h2)) - *out_y;
    
    return true;
}

// 安全的内存分配，带错误检查
void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to allocate memory of size %zu", size);
        set_error(COMPOSITOR_ERROR_MEMORY, "Memory allocation failed");
    }
    return ptr;
}

// 安全的内存重新分配
void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to reallocate memory to size %zu", size);
        set_error(COMPOSITOR_ERROR_MEMORY, "Memory reallocation failed");
        free(ptr);
    }
    return new_ptr;
}

// 安全的字符串复制
char* safe_strdup(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char* new_str = (char*)safe_malloc(len);
    if (new_str) {
        memcpy(new_str, str, len);
    }
    return new_str;
}

// 检查字符串是否为空
bool is_empty_string(const char* str) {
    return (str == NULL || *str == '\0');
}

// 限制值在指定范围内
int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// 限制浮点数在指定范围内
float clamp_float(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// 计算两点之间的距离
float calculate_distance(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return (float)sqrt(dx * dx + dy * dy);
}

// 清理性能统计数据
void cleanup_performance_stats(void) {
    memset(g_frame_times, 0, sizeof(g_frame_times));
    g_fps = 0.0f;
    g_frame_time_index = 0;
    g_last_frame_time = 0;
}

// 清理工具模块
void compositor_utils_cleanup(void) {
    cleanup_performance_stats();
    g_last_error = COMPOSITOR_OK;
    g_error_message[0] = '\0';
    g_current_log_level = COMPOSITOR_LOG_INFO;
}