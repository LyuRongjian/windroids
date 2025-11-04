#include "compositor_utils.h"
#include "compositor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <stdarg.h>

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
static PerformanceStats g_performance_stats = {
    .fps = 0.0f,
    .avg_frame_time = 0.0f,
    .min_frame_time = 0.0f,
    .max_frame_time = 0.0f,
    .render_time = 0.0f,
    .cpu_usage = 0.0f,
    .dirty_rect_count = 0,
    .visible_windows = 0
};

static float g_frame_times[60] = {0.0f};
static int g_frame_time_index = 0;
static clock_t g_last_frame_time = 0;
static clock_t g_render_start_time = 0;

// 内存跟踪数据
static MemoryTracker g_memory_tracker = {
    .total_allocated = 0,
    .peak_allocated = 0,
    .allocation_count = 0
};

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
        
        // 计算统计数据
        float total_time = 0.0f;
        int valid_frames = 0;
        float min_time = 1.0f;  // 初始值设为较大的值
        float max_time = 0.0f;
        
        for (int i = 0; i < 60; i++) {
            if (g_frame_times[i] > 0) {
                total_time += g_frame_times[i];
                valid_frames++;
                if (g_frame_times[i] < min_time) min_time = g_frame_times[i];
                if (g_frame_times[i] > max_time) max_time = g_frame_times[i];
            }
        }
        
        if (valid_frames > 0) {
            g_performance_stats.avg_frame_time = total_time / valid_frames;
            g_performance_stats.fps = 1.0f / g_performance_stats.avg_frame_time;
            g_performance_stats.min_frame_time = min_time;
            g_performance_stats.max_frame_time = max_time;
        }
        
        // 记录性能警告
        if (g_performance_stats.fps < 30.0f) {
            log_message(COMPOSITOR_LOG_WARN, "Low FPS detected: %.1f", g_performance_stats.fps);
        }
    }
    
    g_last_frame_time = current_time;
}

// 获取当前FPS
float compositor_get_fps(void) {
    return g_performance_stats.fps;
}

// 获取性能统计数据
PerformanceStats* compositor_get_performance_stats(void) {
    return &g_performance_stats;
}

// 开始渲染计时器
void start_render_timer(void) {
    g_render_start_time = clock();
}

// 结束渲染计时器
void end_render_timer(void) {
    clock_t end_time = clock();
    g_performance_stats.render_time = (float)(end_time - g_render_start_time) / CLOCKS_PER_SEC;
}

// 更新CPU使用率（简化实现）
void update_cpu_usage(void) {
    // 这里可以实现更复杂的CPU使用率计算
    // 简化版本：基于渲染时间和帧时间的比率
    static float usage_sum = 0.0f;
    static int usage_count = 0;
    
    if (g_performance_stats.avg_frame_time > 0.0f) {
        float usage = g_performance_stats.render_time / g_performance_stats.avg_frame_time * 100.0f;
        usage_sum += usage;
        usage_count++;
        
        if (usage_count >= 10) {
            g_performance_stats.cpu_usage = usage_sum / usage_count;
            usage_sum = 0.0f;
            usage_count = 0;
            
            // 高CPU使用率警告
            if (g_performance_stats.cpu_usage > 80.0f) {
                log_message(COMPOSITOR_LOG_WARN, "High CPU usage detected: %.1f%%", g_performance_stats.cpu_usage);
            }
        }
    }
}

// 标记脏区域
void mark_dirty_rect(CompositorState* state, int x, int y, int width, int height) {
    if (!state) return;
    
    // 边界检查
    if (width <= 0 || height <= 0) return;
    
    // 裁剪到屏幕边界
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > state->width) width = state->width - x;
    if (y + height > state->height) height = state->height - y;
    
    // 如果还没有初始化脏区域数组，先初始化
    if (!state->dirty_rects && state->dirty_rect_count == 0) {
        state->dirty_rects = (CompositorRect*)safe_malloc(DEFAULT_DIRTY_RECTS_SIZE * sizeof(CompositorRect));
        if (!state->dirty_rects) return;
        state->dirty_rect_capacity = DEFAULT_DIRTY_RECTS_SIZE;
    }
    
    // 如果脏区域数组已满，需要扩容
    if (state->dirty_rect_count >= state->dirty_rect_capacity) {
        int new_capacity = state->dirty_rect_capacity * 2;
        CompositorRect* new_rects = (CompositorRect*)safe_realloc(state->dirty_rects, 
                                                                new_capacity * sizeof(CompositorRect));
        if (!new_rects) return;
        state->dirty_rects = new_rects;
        state->dirty_rect_capacity = new_capacity;
    }
    
    // 检查是否可以与现有脏区域合并
    bool merged = false;
    for (int i = 0; i < state->dirty_rect_count; i++) {
        CompositorRect* existing = &state->dirty_rects[i];
        
        // 检查是否有重叠或相邻
        bool overlap = rects_intersect(x, y, width, height, 
                                      existing->x, existing->y, 
                                      existing->width, existing->height);
        
        if (overlap) {
            // 合并矩形
            int merged_x = (x < existing->x) ? x : existing->x;
            int merged_y = (y < existing->y) ? y : existing->y;
            int merged_right = (x + width > existing->x + existing->width) ? 
                             (x + width) : (existing->x + existing->width);
            int merged_bottom = (y + height > existing->y + existing->height) ? 
                               (y + height) : (existing->y + existing->height);
            
            existing->x = merged_x;
            existing->y = merged_y;
            existing->width = merged_right - merged_x;
            existing->height = merged_bottom - merged_y;
            
            merged = true;
            break;
        }
    }
    
    // 如果没有合并，则添加新的脏区域
    if (!merged) {
        state->dirty_rects[state->dirty_rect_count].x = x;
        state->dirty_rects[state->dirty_rect_count].y = y;
        state->dirty_rects[state->dirty_rect_count].width = width;
        state->dirty_rects[state->dirty_rect_count].height = height;
        state->dirty_rect_count++;
    }
    
    // 更新性能统计
    g_performance_stats.dirty_rect_count = state->dirty_rect_count;
    
    // 标记需要重绘
    state->needs_redraw = true;
}

// 清除脏区域列表
void clear_dirty_rects(CompositorState* state) {
    if (!state) return;
    
    state->dirty_rect_count = 0;
    state->needs_redraw = false;
    g_performance_stats.dirty_rect_count = 0;
}

// 合并脏区域，减少渲染区域数量
int merge_dirty_rects(CompositorRect* rects, int count, CompositorRect* merged, int max_merged) {
    if (!rects || !merged || count <= 0 || max_merged <= 0) return 0;
    
    int merged_count = 0;
    bool* merged_flags = (bool*)alloca(count * sizeof(bool));
    memset(merged_flags, 0, count * sizeof(bool));
    
    for (int i = 0; i < count && merged_count < max_merged; i++) {
        if (merged_flags[i]) continue;
        
        // 取第一个未合并的矩形作为起点
        merged[merged_count] = rects[i];
        merged_flags[i] = true;
        
        bool has_merged;
        do {
            has_merged = false;
            for (int j = 0; j < count; j++) {
                if (merged_flags[j]) continue;
                
                // 检查是否可以合并
                bool overlap = rects_intersect(
                    merged[merged_count].x, merged[merged_count].y,
                    merged[merged_count].width, merged[merged_count].height,
                    rects[j].x, rects[j].y, rects[j].width, rects[j].height
                );
                
                if (overlap) {
                    // 合并矩形
                    int min_x = (merged[merged_count].x < rects[j].x) ? 
                               merged[merged_count].x : rects[j].x;
                    int min_y = (merged[merged_count].y < rects[j].y) ? 
                               merged[merged_count].y : rects[j].y;
                    int max_x = (merged[merged_count].x + merged[merged_count].width > 
                               rects[j].x + rects[j].width) ? 
                               (merged[merged_count].x + merged[merged_count].width) : 
                               (rects[j].x + rects[j].width);
                    int max_y = (merged[merged_count].y + merged[merged_count].height > 
                               rects[j].y + rects[j].height) ? 
                               (merged[merged_count].y + merged[merged_count].height) : 
                               (rects[j].y + rects[j].height);
                    
                    merged[merged_count].x = min_x;
                    merged[merged_count].y = min_y;
                    merged[merged_count].width = max_x - min_x;
                    merged[merged_count].height = max_y - min_y;
                    
                    merged_flags[j] = true;
                    has_merged = true;
                }
            }
        } while (has_merged);
        
        merged_count++;
    }
    
    return merged_count;
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
    } else if (ptr && size > 0) {
        track_memory_allocation(size);
    }
    return ptr;
}

// 安全的内存重新分配
void* safe_realloc(void* ptr, size_t size) {
    // 这里简化处理，实际应该跟踪旧大小并更新
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        log_message(COMPOSITOR_LOG_ERROR, "Failed to reallocate memory to size %zu", size);
        set_error(COMPOSITOR_ERROR_MEMORY, "Memory reallocation failed");
        track_memory_deallocation(size); // 假设之前的大小为size
        free(ptr);
    } else if (new_ptr) {
        // 这里应该先减去旧大小，再加上新大小，但简化处理
        if (size > 0) {
            track_memory_allocation(size);
        }
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

// 安全的内存释放，自动置空指针
void safe_free(void** ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
        // 注意：这里无法准确知道释放的内存大小，所以不更新内存跟踪
    }
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

// 检查点是否在矩形内
bool rect_contains_point(int x, int y, int width, int height, int point_x, int point_y) {
    return (point_x >= x && point_x < x + width && point_y >= y && point_y < y + height);
}

// 扩展矩形
CompositorRect expand_rect(const CompositorRect* rect, int padding) {
    CompositorRect result;
    result.x = rect->x - padding;
    result.y = rect->y - padding;
    result.width = rect->width + padding * 2;
    result.height = rect->height + padding * 2;
    return result;
}

// 内存跟踪函数
void track_memory_allocation(size_t size) {
    g_memory_tracker.total_allocated += size;
    if (g_memory_tracker.total_allocated > g_memory_tracker.peak_allocated) {
        g_memory_tracker.peak_allocated = g_memory_tracker.total_allocated;
    }
    g_memory_tracker.allocation_count++;
    
    // 内存使用警告
    if (g_memory_tracker.total_allocated > 1024 * 1024 * 1024) { // 1GB
        log_message(COMPOSITOR_LOG_WARN, "High memory usage detected: %.2f MB", 
                   (float)g_memory_tracker.total_allocated / (1024 * 1024));
    }
}

void track_memory_deallocation(size_t size) {
    if (g_memory_tracker.total_allocated >= size) {
        g_memory_tracker.total_allocated -= size;
    }
}

MemoryTracker* get_memory_tracker(void) {
    return &g_memory_tracker;
}

void reset_memory_tracker(void) {
    g_memory_tracker.total_allocated = 0;
    g_memory_tracker.peak_allocated = 0;
    g_memory_tracker.allocation_count = 0;
}

// 窗口排序辅助函数
int compare_window_z_order(const void* a, const void* b) {
    // 此函数用于qsort，比较两个窗口的z_order
    // 注意：这里假设a和b是WindowInfo指针或包含z_order字段的结构
    // 实际使用时可能需要根据具体结构体调整
    const struct { int z_order; } *win_a = a;
    const struct { int z_order; } *win_b = b;
    
    // 按z_order升序排序（小的在下面）
    return win_a->z_order - win_b->z_order;
}

// 字符串格式化函数
char* format_string(const char* format, ...) {
    if (!format) return NULL;
    
    va_list args;
    va_start(args, format);
    
    // 先计算需要的缓冲区大小
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);
    
    if (len < 0) return NULL;
    
    // 分配内存
    char* buffer = (char*)safe_malloc(len + 1);
    if (!buffer) return NULL;
    
    // 实际格式化字符串
    va_start(args, format);
    vsnprintf(buffer, len + 1, format, args);
    va_end(args);
    
    return buffer;
}

// 时间工具函数 - 获取当前时间（毫秒）
unsigned long get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

// 清理性能统计数据
void cleanup_performance_stats(void) {
    memset(g_frame_times, 0, sizeof(g_frame_times));
    memset(&g_performance_stats, 0, sizeof(PerformanceStats));
    g_frame_time_index = 0;
    g_last_frame_time = 0;
    g_render_start_time = 0;
}

// 清理内存跟踪数据
void cleanup_memory_tracker(void) {
    reset_memory_tracker();
}

// 清理工具模块
void compositor_utils_cleanup(void) {
    cleanup_performance_stats();
    cleanup_memory_tracker();
    g_last_error = COMPOSITOR_OK;
    g_error_message[0] = '\0';
    g_current_log_level = COMPOSITOR_LOG_INFO;
}