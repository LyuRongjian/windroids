#include "compositor_perf.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>

#define LOG_TAG "PerfMonitor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// 全局性能监控器状态
static struct perf_monitor g_monitor = {0};
static struct profiler g_profiler = {0};
static struct perf_thresholds g_thresholds = {
    .min_fps = 30.0f,
    .max_frame_time = 33.3f, // 30 FPS
    .max_memory_usage = 512 * 1024 * 1024, // 512MB
    .max_cpu_usage = 80.0f, // 80%
    .max_gpu_usage = 80.0f  // 80%
};
static perf_warning_callback_t g_warning_callback = NULL;
static void* g_warning_user_data = NULL;

// 内部函数声明
static uint64_t perf_get_time(void);
static void perf_update_counters(void);

// 初始化性能监控器
int perf_monitor_init(void) {
    if (g_monitor.enabled) {
        LOGE("Performance monitor already initialized");
        return -1;
    }
    
    memset(&g_monitor, 0, sizeof(g_monitor));
    g_monitor.enabled = true;
    g_monitor.update_interval = 60; // 每60帧更新一次
    g_monitor.min_frame_time = 1000.0f; // 初始最小值
    g_monitor.last_frame_time = perf_get_time();
    
    LOGI("Performance monitor initialized");
    return 0;
}

// 销毁性能监控器
void perf_monitor_destroy(void) {
    memset(&g_monitor, 0, sizeof(g_monitor));
    LOGI("Performance monitor destroyed");
}

// 启用/禁用性能监控
void perf_monitor_set_enabled(bool enabled) {
    g_monitor.enabled = enabled;
}

// 设置更新间隔
void perf_monitor_set_update_interval(uint32_t interval) {
    if (interval == 0) interval = 1;
    g_monitor.update_interval = interval;
}

// 开始帧
void perf_monitor_begin_frame(void) {
    if (!g_monitor.enabled) return;
    
    g_monitor.last_frame_time = perf_get_time();
}

// 结束帧
void perf_monitor_end_frame(void) {
    if (!g_monitor.enabled) return;
    
    uint64_t current_time = perf_get_time();
    float frame_time = (float)(current_time - g_monitor.last_frame_time) / 1000.0f; // 毫秒
    
    // 更新帧时间统计
    if (frame_time < g_monitor.min_frame_time) {
        g_monitor.min_frame_time = frame_time;
    }
    if (frame_time > g_monitor.max_frame_time) {
        g_monitor.max_frame_time = frame_time;
    }
    
    // 更新平均帧时间
    g_monitor.avg_frame_time = (g_monitor.avg_frame_time * (g_monitor.frame_count - 1) + frame_time) / g_monitor.frame_count;
    
    // 更新帧率
    if (frame_time > 0.0f) {
        g_monitor.fps = 1000.0f / frame_time;
    }
    
    // 增加帧计数
    g_monitor.frame_count++;
    g_monitor.frame_since_update++;
    
    // 更新计数器
    perf_update_counters();
    
    // 检查是否需要更新统计
    if (g_monitor.frame_since_update >= g_monitor.update_interval) {
        g_monitor.frame_since_update = 0;
        
        // 更新计数器平均值和峰值
        for (int i = 0; i < PERF_COUNTER_COUNT; i++) {
            // 平均值
            g_monitor.counter_averages[i] = (float)g_monitor.counter_totals[i] / g_monitor.frame_count;
            
            // 峰值
            if (g_monitor.counters[i] > g_monitor.counter_peaks[i]) {
                g_monitor.counter_peaks[i] = g_monitor.counters[i];
            }
        }
        
        // 检查性能警告
        perf_check_warnings();
    }
}

// 更新计数器
void perf_monitor_update_counter(perf_counter_type_t type, uint64_t value) {
    if (!g_monitor.enabled || type < 0 || type >= PERF_COUNTER_COUNT) {
        return;
    }
    
    g_monitor.counters[type] = value;
    g_monitor.counter_totals[type] += value;
}

// 增加计数器
void perf_monitor_increment_counter(perf_counter_type_t type) {
    if (!g_monitor.enabled || type < 0 || type >= PERF_COUNTER_COUNT) {
        return;
    }
    
    g_monitor.counters[type]++;
    g_monitor.counter_totals[type]++;
}

// 获取当前帧率
float perf_monitor_get_fps(void) {
    return g_monitor.fps;
}

// 获取平均帧时间
float perf_monitor_get_avg_frame_time(void) {
    return g_monitor.avg_frame_time;
}

// 获取最小帧时间
float perf_monitor_get_min_frame_time(void) {
    return g_monitor.min_frame_time;
}

// 获取最大帧时间
float perf_monitor_get_max_frame_time(void) {
    return g_monitor.max_frame_time;
}

// 获取计数器值
uint64_t perf_monitor_get_counter(perf_counter_type_t type) {
    if (type < 0 || type >= PERF_COUNTER_COUNT) {
        return 0;
    }
    
    return g_monitor.counters[type];
}

// 获取计数器平均值
float perf_monitor_get_counter_average(perf_counter_type_t type) {
    if (type < 0 || type >= PERF_COUNTER_COUNT) {
        return 0.0f;
    }
    
    return g_monitor.counter_averages[type];
}

// 获取计数器峰值
float perf_monitor_get_counter_peak(perf_counter_type_t type) {
    if (type < 0 || type >= PERF_COUNTER_COUNT) {
        return 0.0f;
    }
    
    return g_monitor.counter_peaks[type];
}

// 重置性能监控器
void perf_monitor_reset(void) {
    memset(&g_monitor.counters, 0, sizeof(g_monitor.counters));
    memset(&g_monitor.counter_totals, 0, sizeof(g_monitor.counter_totals));
    memset(&g_monitor.counter_averages, 0, sizeof(g_monitor.counter_averages));
    memset(&g_monitor.counter_peaks, 0, sizeof(g_monitor.counter_peaks));
    
    g_monitor.frame_count = 0;
    g_monitor.frame_since_update = 0;
    g_monitor.fps = 0.0f;
    g_monitor.avg_frame_time = 0.0f;
    g_monitor.min_frame_time = 1000.0f;
    g_monitor.max_frame_time = 0.0f;
    g_monitor.last_frame_time = perf_get_time();
}

// 初始化性能分析器
int profiler_init(uint32_t max_samples) {
    if (g_profiler.enabled) {
        LOGE("Profiler already initialized");
        return -1;
    }
    
    if (max_samples == 0) {
        max_samples = 1000; // 默认1000个样本
    }
    
    memset(&g_profiler, 0, sizeof(g_profiler));
    g_profiler.enabled = true;