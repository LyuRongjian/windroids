#include "compositor_input_performance.h"
#include "compositor_utils.h"
#include <string.h>
#include <time.h>

// 输入设备性能统计
static struct {
    int64_t last_input_time;        // 上次输入时间
    int input_frequency;            // 输入频率（次/秒）
    int total_input_count;           // 总输入次数
    int device_usage_count[10];      // 各设备类型使用次数
    int64_t total_response_time;     // 总响应时间
    int64_t max_response_time;       // 最大响应时间
    int64_t min_response_time;       // 最小响应时间
} g_input_performance_stats = {
    .last_input_time = 0,
    .input_frequency = 0,
    .total_input_count = 0,
    .device_usage_count = {0},
    .total_response_time = 0,
    .max_response_time = 0,
    .min_response_time = LLONG_MAX
};

// 设备特定配置
static struct {
    bool device_type_supported[10]; // 支持的设备类型
    int max_simultaneous_touches;   // 最大同时触摸点数
    int device_priority[10];        // 设备优先级（数值越高优先级越高）
    bool adaptive_input;            // 自适应输入处理
    int input_response_time;        // 输入响应时间（毫秒）
} g_input_device_config = {
    .device_type_supported = {false}, // 初始化为全不支持
    .max_simultaneous_touches = 10,
    .device_priority = {0},        // 初始化优先级为0
    .adaptive_input = true,         // 默认启用自适应输入
    .input_response_time = 5        // 默认5毫秒响应时间
};

// 获取当前时间（毫秒）
static int64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 初始化输入性能统计模块
int compositor_input_performance_init(void) {
    // 重置性能统计
    memset(&g_input_performance_stats, 0, sizeof(g_input_performance_stats));
    g_input_performance_stats.min_response_time = LLONG_MAX;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Input performance module initialized");
    return 0;
}

// 清理输入性能统计模块
void compositor_input_performance_cleanup(void) {
    memset(&g_input_performance_stats, 0, sizeof(g_input_performance_stats));
    log_message(COMPOSITOR_LOG_DEBUG, "Input performance module cleaned up");
}

// 更新输入性能统计
void compositor_input_performance_update_stats(CompositorInputDeviceType device_type, int64_t response_time) {
    int64_t current_time = get_current_time_ms();
    
    // 更新输入频率
    if (g_input_performance_stats.last_input_time > 0) {
        int64_t time_diff = current_time - g_input_performance_stats.last_input_time;
        if (time_diff > 0) {
            g_input_performance_stats.input_frequency = 1000 / time_diff;
        }
    }
    g_input_performance_stats.last_input_time = current_time;
    
    // 更新设备使用次数
    if (device_type >= 0 && device_type < 10) {
        g_input_performance_stats.device_usage_count[device_type]++;
    }
    
    // 更新总输入次数
    g_input_performance_stats.total_input_count++;
    
    // 更新响应时间统计
    if (response_time > 0) {
        g_input_performance_stats.total_response_time += response_time;
        if (response_time > g_input_performance_stats.max_response_time) {
            g_input_performance_stats.max_response_time = response_time;
        }
        if (response_time < g_input_performance_stats.min_response_time) {
            g_input_performance_stats.min_response_time = response_time;
        }
    }
}

// 获取输入性能统计
int compositor_input_performance_get_stats(CompositorInputPerformanceStats* stats) {
    if (!stats) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    stats->input_frequency = g_input_performance_stats.input_frequency;
    stats->total_input_count = g_input_performance_stats.total_input_count;
    
    // 计算平均响应时间
    if (g_input_performance_stats.total_input_count > 0) {
        stats->average_response_time = g_input_performance_stats.total_response_time / 
                                      g_input_performance_stats.total_input_count;
    } else {
        stats->average_response_time = 0;
    }
    
    stats->max_response_time = g_input_performance_stats.max_response_time;
    stats->min_response_time = g_input_performance_stats.min_response_time == LLONG_MAX ? 
                              0 : g_input_performance_stats.min_response_time;
    
    // 复制设备使用次数
    for (int i = 0; i < 10; i++) {
        stats->device_usage_count[i] = g_input_performance_stats.device_usage_count[i];
    }
    
    return COMPOSITOR_OK;
}

// 自适应调整输入处理参数
void compositor_input_performance_adapt_processing(void) {
    // 根据输入频率调整响应时间
    if (g_input_performance_stats.input_frequency > 60) {
        // 高频率输入，减少响应时间
        g_input_device_config.input_response_time = 2;
    } else if (g_input_performance_stats.input_frequency > 30) {
        // 中等频率输入，适中的响应时间
        g_input_device_config.input_response_time = 5;
    } else {
        // 低频率输入，可以增加响应时间以节省资源
        g_input_device_config.input_response_time = 10;
    }
    
    // 根据设备使用情况调整优先级
    int most_used_device_type = 0;
    int max_usage = 0;
    
    for (int i = 0; i < 10; i++) {
        if (g_input_performance_stats.device_usage_count[i] > max_usage) {
            max_usage = g_input_performance_stats.device_usage_count[i];
            most_used_device_type = i;
        }
    }
    
    // 提高最常用设备的优先级
    if (max_usage > 0) {
        g_input_device_config.device_priority[most_used_device_type] = 10;
        
        // 降低其他设备的优先级
        for (int i = 0; i < 10; i++) {
            if (i != most_used_device_type) {
                g_input_device_config.device_priority[i] = 5;
            }
        }
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Adapted input processing: response_time=%dms", 
               g_input_device_config.input_response_time);
}

// 获取输入频率
int compositor_input_performance_get_frequency(void) {
    return g_input_performance_stats.input_frequency;
}

// 重置性能统计
void compositor_input_performance_reset_stats(void) {
    memset(&g_input_performance_stats, 0, sizeof(g_input_performance_stats));
    g_input_performance_stats.min_response_time = LLONG_MAX;
    log_message(COMPOSITOR_LOG_DEBUG, "Input performance stats reset");
}