#ifndef COMPOSITOR_PERF_H
#define COMPOSITOR_PERF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 性能计数器类型
typedef enum {
    PERF_COUNTER_FRAME_TIME,      // 帧时间
    PERF_COUNTER_DRAW_CALLS,       // 绘制调用次数
    PERF_COUNTER_TRIANGLES,        // 三角形数量
    PERF_COUNTER_TEXTURE_SWITCHES, // 纹理切换次数
    PERF_COUNTER_MEMORY_USAGE,     // 内存使用量
    PERF_COUNTER_CPU_USAGE,        // CPU使用率
    PERF_COUNTER_GPU_USAGE,        // GPU使用率
    PERF_COUNTER_COUNT             // 计数器数量
} perf_counter_type_t;

// 性能监控器状态
struct perf_monitor {
    uint32_t frame_count;              // 帧计数
    uint64_t last_frame_time;          // 上一帧时间
    float fps;                         // 帧率
    float avg_frame_time;              // 平均帧时间
    float min_frame_time;              // 最小帧时间
    float max_frame_time;              // 最大帧时间
    uint64_t counters[PERF_COUNTER_COUNT]; // 性能计数器
    uint64_t counter_totals[PERF_COUNTER_COUNT]; // 计数器总计
    float counter_averages[PERF_COUNTER_COUNT]; // 计数器平均值
    float counter_peaks[PERF_COUNTER_COUNT]; // 计数器峰值
    bool enabled;                      // 是否启用
    uint32_t update_interval;          // 更新间隔（帧数）
    uint32_t frame_since_update;       // 自上次更新以来的帧数
};

// 性能分析器
struct profiler {
    bool enabled;                      // 是否启用
    uint32_t max_samples;               // 最大样本数
    uint32_t sample_count;              // 当前样本数
    uint32_t current_sample;            // 当前样本索引
    float* frame_times;                // 帧时间样本
    float* cpu_usage;                  // CPU使用率样本
    float* gpu_usage;                  // GPU使用率样本
    uint64_t* draw_calls;              // 绘制调用样本
    uint64_t* triangles;               // 三角形数量样本
};

// 性能警告阈值
struct perf_thresholds {
    float min_fps;                     // 最小帧率阈值
    float max_frame_time;              // 最大帧时间阈值
    uint64_t max_memory_usage;         // 最大内存使用阈值
    float max_cpu_usage;               // 最大CPU使用率阈值
    float max_gpu_usage;               // 最大GPU使用率阈值
};

// 性能警告回调
typedef void (*perf_warning_callback_t)(const char* message, void* user_data);

// 初始化性能监控器
int perf_monitor_init(void);

// 销毁性能监控器
void perf_monitor_destroy(void);

// 启用/禁用性能监控
void perf_monitor_set_enabled(bool enabled);

// 设置更新间隔
void perf_monitor_set_update_interval(uint32_t interval);

// 开始帧
void perf_monitor_begin_frame(void);

// 结束帧
void perf_monitor_end_frame(void);

// 更新计数器
void perf_monitor_update_counter(perf_counter_type_t type, uint64_t value);

// 增加计数器
void perf_monitor_increment_counter(perf_counter_type_t type);

// 获取当前帧率
float perf_monitor_get_fps(void);

// 获取平均帧时间
float perf_monitor_get_avg_frame_time(void);

// 获取最小帧时间
float perf_monitor_get_min_frame_time(void);

// 获取最大帧时间
float perf_monitor_get_max_frame_time(void);

// 获取计数器值
uint64_t perf_monitor_get_counter(perf_counter_type_t type);

// 获取计数器平均值
float perf_monitor_get_counter_average(perf_counter_type_t type);

// 获取计数器峰值
float perf_monitor_get_counter_peak(perf_counter_type_t type);

// 重置性能监控器
void perf_monitor_reset(void);

// 初始化性能分析器
int profiler_init(uint32_t max_samples);

// 销毁性能分析器
void profiler_destroy(void);

// 启用/禁用性能分析器
void profiler_set_enabled(bool enabled);

// 添加样本
void profiler_add_sample(float frame_time, float cpu_usage, float gpu_usage, 
                        uint64_t draw_calls, uint64_t triangles);

// 获取帧时间样本
const float* profiler_get_frame_time_samples(uint32_t* count);

// 获取CPU使用率样本
const float* profiler_get_cpu_usage_samples(uint32_t* count);

// 获取GPU使用率样本
const float* profiler_get_gpu_usage_samples(uint32_t* count);

// 获取绘制调用样本
const uint64_t* profiler_get_draw_calls_samples(uint32_t* count);

// 获取三角形数量样本
const uint64_t* profiler_get_triangles_samples(uint32_t* count);

// 设置性能警告阈值
void perf_set_thresholds(const struct perf_thresholds* thresholds);

// 获取性能警告阈值
void perf_get_thresholds(struct perf_thresholds* thresholds);

// 设置性能警告回调
void perf_set_warning_callback(perf_warning_callback_t callback, void* user_data);

// 检查性能警告
void perf_check_warnings(void);

// 打印性能统计
void perf_print_stats(void);

// 更新性能监控（每帧调用）
void perf_update(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_PERF_H