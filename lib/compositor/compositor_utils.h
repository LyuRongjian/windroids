#ifndef COMPOSITOR_UTILS_H
#define COMPOSITOR_UTILS_H

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>
#include <stdint.h>

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
    COMPOSITOR_ERROR_UNEXPECTED = -19,
    COMPOSITOR_ERROR_NO_ACTIVE_WINDOW = -20,
    COMPOSITOR_ERROR_WINDOW_OPERATION_FAILED = -21,
    COMPOSITOR_ERROR_RENDER_ERROR = -22,
    COMPOSITOR_ERROR_TEXTURE_CACHE_ERROR = -23,
    COMPOSITOR_ERROR_CPU_USAGE_HIGH = -24
};

// 日志级别定义
enum {
    COMPOSITOR_LOG_ERROR = 0,
    COMPOSITOR_LOG_WARN = 1,
    COMPOSITOR_LOG_INFO = 2,
    COMPOSITOR_LOG_DEBUG = 3
};
typedef int CompositorLogLevel;

// 全局变量成功状态
#define COMPOSITOR_SUCCESS COMPOSITOR_OK

// 日志标签
#define LOG_TAG "WinDroidsCompositor"

// 向前声明CompositorState结构体
typedef struct CompositorState CompositorState;

// 日志和错误处理函数
void log_message(int level, const char* format, ...);
void set_error(int error_code, const char* format, ...);
const char* get_error_description(int error_code);

// 公共错误处理函数
int compositor_get_last_error(void);
const char* compositor_get_error_message(char* buffer, size_t size);

// 日志级别控制
void utils_set_log_level(int level);

// 矩形结构体定义
typedef struct CompositorRect {
    int32_t x, y, width, height;
} CompositorRect;

// 颜色结构体
typedef struct CompositorColor {
    float r;
    float g;
    float b;
    float a;
} CompositorColor;

// 性能统计结构体
typedef struct CompositorPerfStat {
    float fps;
    float avg_frame_time;
    float min_frame_time;
    float max_frame_time;
    float render_time;
    float cpu_usage;
    int dirty_rect_count;
    int visible_windows;
    char name[64];  // 统计名称
} CompositorPerfStat;
typedef CompositorPerfStat PerformanceStats;

// 内存跟踪结构体
typedef struct CompositorMemoryStats {
    uint64_t total_allocated;
    uint64_t peak_allocated;
    uint64_t allocation_count;
    uint64_t free_count;
    uint64_t failed_allocations;
    uint64_t max_memory_limit;
    bool track_leaks;
    // 按类型统计
    uint64_t texture_memory_bytes;
    uint64_t buffer_memory_bytes;
} CompositorMemoryStats;
typedef CompositorMemoryStats MemoryTracker;

// 性能相关函数
void update_performance_stats(void);
float compositor_get_fps(void);
PerformanceStats* compositor_get_performance_stats(void);
void start_render_timer(void);
void end_render_timer(void);
void update_cpu_usage(void);

// 渲染相关工具函数
void mark_dirty_rect(CompositorState* state, int x, int y, int width, int height);
void clear_dirty_rects(CompositorState* state);
int merge_dirty_rects(CompositorRect* rects, int count, CompositorRect* merged, int max_merged);

// 矩形操作函数
bool rects_intersect(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2);
bool calculate_intersection(int x1, int y1, int w1, int h1, 
                           int x2, int y2, int w2, int h2,
                           int* out_x, int* out_y, int* out_w, int* out_h);
bool rect_contains_point(int x, int y, int width, int height, int point_x, int point_y);
CompositorRect expand_rect(const CompositorRect* rect, int padding);

// 内存管理工具
void* safe_malloc(size_t size, const char* label);
void* safe_realloc(void* ptr, size_t size, const char* label);
char* safe_strdup(const char* str);
void safe_free(void** ptr);

// 内存检查函数
bool check_memory_usage(void);

// 内存跟踪函数
void track_memory_allocation(size_t size);
void track_memory_deallocation(size_t size);
MemoryTracker* get_memory_tracker(void);
void reset_memory_tracker(void);

// 字符串工具函数
bool is_empty_string(const char* str);

// 数学工具函数
int clamp_int(int value, int min, int max);
float clamp_float(float value, float min, float max);
float calculate_distance(int x1, int y1, int x2, int y2);

// 窗口排序辅助函数
int compare_window_z_order(const void* a, const void* b);

// 字符串格式化函数
char* format_string(const char* format, ...);

// 时间工具函数
unsigned long get_current_time_ms(void);
uint64_t compositor_get_current_time_us(void);
uint64_t compositor_get_current_time_ns(void);

// 调试辅助
void compositor_debug_dump_stack(void);
const char* compositor_error_to_string(int error_code);

// 资源清理函数
void cleanup_performance_stats(void);
void cleanup_memory_tracker(void);
void compositor_utils_cleanup(void);

#endif // COMPOSITOR_UTILS_H