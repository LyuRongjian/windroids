#include "compositor_monitor.h"
#include "compositor.h"
#include "compositor_perf.h"
#include "compositor_render.h"
#include <android/log.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define LOG_TAG "Monitor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// 默认缓冲区大小
#define DEFAULT_BUFFER_SIZE 1024

// 监控状态
static struct {
    bool initialized;
    struct monitor_settings settings;
    struct monitor_data_buffer data_buffers[MONITOR_DATA_TYPE_COUNT];
    uint64_t last_sample_time;
    uint64_t last_report_time;
    monitor_callback_t callback;
    void* callback_user_data;
} g_monitor_state = {0};

// 内部函数声明
static void sample_data(void);
static void generate_reports(void);
static void auto_save_reports(void);
static void init_data_buffer(struct monitor_data_buffer* buffer, uint32_t capacity);
static void free_data_buffer(struct monitor_data_buffer* buffer);
static void add_data_point_to_buffer(struct monitor_data_buffer* buffer, const struct monitor_data_point* point);
static void calculate_statistics(const struct monitor_data_buffer* buffer, struct monitor_statistics* stats);
static int compare_float(const void* a, const void* b);
static char* generate_summary_text(const struct monitor_statistics* stats, monitor_data_type_t type);
static char* generate_detailed_text(const struct monitor_statistics* stats, monitor_data_type_t type);
static char* generate_chart_data(const struct monitor_data_buffer* buffer);
static uint64_t get_current_time(void);
static const char* get_data_type_name(monitor_data_type_t type);
static const char* get_data_type_unit(monitor_data_type_t type);

// 初始化监控模块
int monitor_init(void) {
    if (g_monitor_state.initialized) {
        LOGE("Monitor module already initialized");
        return -1;
    }
    
    memset(&g_monitor_state, 0, sizeof(g_monitor_state));
    
    // 设置默认监控设置
    g_monitor_state.settings.enabled = false;
    g_monitor_state.settings.auto_save = false;
    g_monitor_state.settings.real_time_analysis = false;
    g_monitor_state.settings.buffer_size = DEFAULT_BUFFER_SIZE;
    g_monitor_state.settings.sample_interval_ms = 100;  // 100ms
    g_monitor_state.settings.report_interval_ms = 5000; // 5s
    g_monitor_state.settings.save_path = strdup("/data/local/tmp/compositor_monitor");
    g_monitor_state.settings.include_charts = true;
    g_monitor_state.settings.include_detailed_stats = true;
    
    // 初始化数据缓冲区
    for (int i = 0; i < MONITOR_DATA_TYPE_COUNT; i++) {
        init_data_buffer(&g_monitor_state.data_buffers[i], g_monitor_state.settings.buffer_size);
    }
    
    g_monitor_state.last_sample_time = get_current_time();
    g_monitor_state.last_report_time = get_current_time();
    
    g_monitor_state.initialized = true;
    
    LOGI("Monitor module initialized");
    return 0;
}

// 销毁监控模块
void monitor_destroy(void) {
    if (!g_monitor_state.initialized) {
        return;
    }
    
    // 释放数据缓冲区
    for (int i = 0; i < MONITOR_DATA_TYPE_COUNT; i++) {
        free_data_buffer(&g_monitor_state.data_buffers[i]);
    }
    
    // 释放保存路径
    if (g_monitor_state.settings.save_path) {
        free(g_monitor_state.settings.save_path);
        g_monitor_state.settings.save_path = NULL;
    }
    
    g_monitor_state.initialized = false;
    g_monitor_state.callback = NULL;
    g_monitor_state.callback_user_data = NULL;
    
    LOGI("Monitor module destroyed");
}

// 更新监控模块（每帧调用）
void monitor_update(void) {
    if (!g_monitor_state.initialized || !g_monitor_state.settings.enabled) {
        return;
    }
    
    uint64_t current_time = get_current_time();
    
    // 检查是否需要采样数据
    if (current_time - g_monitor_state.last_sample_time >= 
        g_monitor_state.settings.sample_interval_ms * 1000000ULL) {
        sample_data();
        g_monitor_state.last_sample_time = current_time;
    }
    
    // 检查是否需要生成报告
    if (current_time - g_monitor_state.last_report_time >= 
        g_monitor_state.settings.report_interval_ms * 1000000ULL) {
        generate_reports();
        g_monitor_state.last_report_time = current_time;
    }
}

// 启用/禁用监控
void monitor_set_enabled(bool enabled) {
    g_monitor_state.settings.enabled = enabled;
    LOGI("Monitor %s", enabled ? "enabled" : "disabled");
}

// 检查监控是否启用
bool monitor_is_enabled(void) {
    return g_monitor_state.settings.enabled;
}

// 设置监控设置
void monitor_set_settings(const struct monitor_settings* settings) {
    if (!settings) {
        LOGE("Invalid settings pointer");
        return;
    }
    
    // 释放旧的保存路径
    if (g_monitor_state.settings.save_path) {
        free(g_monitor_state.settings.save_path);
    }
    
    // 复制设置
    g_monitor_state.settings = *settings;
    
    // 复制保存路径
    if (settings->save_path) {
        g_monitor_state.settings.save_path = strdup(settings->save_path);
    } else {
        g_monitor_state.settings.save_path = strdup("/data/local/tmp/compositor_monitor");
    }
    
    // 调整缓冲区大小
    if (settings->buffer_size != g_monitor_state.settings.buffer_size) {
        for (int i = 0; i < MONITOR_DATA_TYPE_COUNT; i++) {
            free_data_buffer(&g_monitor_state.data_buffers[i]);
            init_data_buffer(&g_monitor_state.data_buffers[i], settings->buffer_size);
        }
    }
    
    LOGI("Monitor settings updated");
}

// 获取监控设置
void monitor_get_settings(struct monitor_settings* settings) {
    if (!settings) {
        LOGE("Invalid settings pointer");
        return;
    }
    
    *settings = g_monitor_state.settings;
}

// 启用/禁用自动保存
void monitor_set_auto_save_enabled(bool enabled) {
    g_monitor_state.settings.auto_save = enabled;
    LOGI("Auto save %s", enabled ? "enabled" : "disabled");
}

// 检查自动保存是否启用
bool monitor_is_auto_save_enabled(void) {
    return g_monitor_state.settings.auto_save;
}

// 设置采样间隔
void monitor_set_sample_interval(uint32_t interval_ms) {
    if (interval_ms == 0) {
        LOGE("Invalid sample interval: %u", interval_ms);
        return;
    }
    
    g_monitor_state.settings.sample_interval_ms = interval_ms;
    LOGI("Sample interval set to %u ms", interval_ms);
}

// 获取采样间隔
uint32_t monitor_get_sample_interval(void) {
    return g_monitor_state.settings.sample_interval_ms;
}

// 设置报告生成间隔
void monitor_set_report_interval(uint32_t interval_ms) {
    if (interval_ms == 0) {
        LOGE("Invalid report interval: %u", interval_ms);
        return;
    }
    
    g_monitor_state.settings.report_interval_ms = interval_ms;
    LOGI("Report interval set to %u ms", interval_ms);
}

// 获取报告生成间隔
uint32_t monitor_get_report_interval(void) {
    return g_monitor_state.settings.report_interval_ms;
}

// 设置保存路径
void monitor_set_save_path(const char* path) {
    if (!path) {
        LOGE("Invalid save path");
        return;
    }
    
    if (g_monitor_state.settings.save_path) {
        free(g_monitor_state.settings.save_path);
    }
    
    g_monitor_state.settings.save_path = strdup(path);
    LOGI("Save path set to %s", path);
}

// 获取保存路径
const char* monitor_get_save_path(void) {
    return g_monitor_state.settings.save_path;
}

// 启用/禁用实时分析
void monitor_set_real_time_analysis_enabled(bool enabled) {
    g_monitor_state.settings.real_time_analysis = enabled;
    LOGI("Real-time analysis %s", enabled ? "enabled" : "disabled");
}

// 检查实时分析是否启用
bool monitor_is_real_time_analysis_enabled(void) {
    return g_monitor_state.settings.real_time_analysis;
}

// 启用/禁用图表生成
void monitor_set_charts_enabled(bool enabled) {
    g_monitor_state.settings.include_charts = enabled;
    LOGI("Charts %s", enabled ? "enabled" : "disabled");
}

// 检查图表生成是否启用
bool monitor_is_charts_enabled(void) {
    return g_monitor_state.settings.include_charts;
}

// 启用/禁用详细统计
void monitor_set_detailed_stats_enabled(bool enabled) {
    g_monitor_state.settings.include_detailed_stats = enabled;
    LOGI("Detailed stats %s", enabled ? "enabled" : "disabled");
}

// 检查详细统计是否启用
bool monitor_is_detailed_stats_enabled(void) {
    return g_monitor_state.settings.include_detailed_stats;
}

// 添加监控数据点
void monitor_add_data_point(monitor_data_type_t type, float value) {
    if (type >= MONITOR_DATA_TYPE_COUNT) {
        LOGE("Invalid data type: %d", type);
        return;
    }
    
    struct monitor_data_point point;
    point.timestamp = get_current_time();
    point.type = type;
    point.value = value;
    
    add_data_point_to_buffer(&g_monitor_state.data_buffers[type], &point);
}

// 获取监控数据缓冲区
void monitor_get_data_buffer(monitor_data_type_t type, struct monitor_data_buffer* buffer) {
    if (type >= MONITOR_DATA_TYPE_COUNT) {
        LOGE("Invalid data type: %d", type);
        return;
    }
    
    if (!buffer) {
        LOGE("Invalid buffer pointer");
        return;
    }
    
    *buffer = g_monitor_state.data_buffers[type];
}

// 获取监控统计信息
void monitor_get_statistics(monitor_data_type_t type, struct monitor_statistics* stats) {
    if (type >= MONITOR_DATA_TYPE_COUNT) {
        LOGE("Invalid data type: %d", type);
        return;
    }
    
    if (!stats) {
        LOGE("Invalid stats pointer");
        return;
    }
    
    calculate_statistics(&g_monitor_state.data_buffers[type], stats);
}

// 生成监控报告
struct monitor_report* monitor_generate_report(monitor_data_type_t type) {
    if (type >= MONITOR_DATA_TYPE_COUNT) {
        LOGE("Invalid data type: %d", type);
        return NULL;
    }
    
    struct monitor_report* report = malloc(sizeof(struct monitor_report));
    if (!report) {
        LOGE("Failed to allocate memory for report");
        return NULL;
    }
    
    memset(report, 0, sizeof(struct monitor_report));
    report->type = type;
    
    // 计算统计信息
    calculate_statistics(&g_monitor_state.data_buffers[type], &report->stats);
    
    // 生成摘要文本
    report->summary_text = generate_summary_text(&report->stats, type);
    
    // 生成详细文本
    if (g_monitor_state.settings.include_detailed_stats) {
        report->detailed_text = generate_detailed_text(&report->stats, type);
    }
    
    // 生成图表数据
    if (g_monitor_state.settings.include_charts) {
        report->chart_data = generate_chart_data(&g_monitor_state.data_buffers[type]);
    }
    
    return report;
}

// 保存监控报告
int monitor_save_report(const struct monitor_report* report, const char* path) {
    if (!report) {
        LOGE("Invalid report pointer");
        return -1;
    }
    
    if (!path) {
        LOGE("Invalid path");
        return -1;
    }
    
    FILE* file = fopen(path, "w");
    if (!file) {
        LOGE("Failed to open file for writing: %s", path);
        return -1;
    }
    
    // 写入报告类型
    fprintf(file, "Report Type: %s\n", get_data_type_name(report->type));
    
    // 写入统计信息
    fprintf(file, "Statistics:\n");
    fprintf(file, "  Min Value: %.2f %s\n", report->stats.min_value, get_data_type_unit(report->type));
    fprintf(file, "  Max Value: %.2f %s\n", report->stats.max_value, get_data_type_unit(report->type));
    fprintf(file, "  Avg Value: %.2f %s\n", report->stats.avg_value, get_data_type_unit(report->type));
    fprintf(file, "  Median Value: %.2f %s\n", report->stats.median_value, get_data_type_unit(report->type));
    fprintf(file, "  Std Deviation: %.2f %s\n", report->stats.std_deviation, get_data_type_unit(report->type));
    fprintf(file, "  Sample Count: %u\n", report->stats.sample_count);
    
    // 写入时间范围
    if (report->stats.sample_count > 0) {
        time_t start_time = report->stats.first_timestamp / 1000000000ULL;
        time_t end_time = report->stats.last_timestamp / 1000000000ULL;
        fprintf(file, "  Time Range: %s", ctime(&start_time));
        fprintf(file, "  To: %s", ctime(&end_time));
    }
    
    // 写入摘要文本
    if (report->summary_text) {
        fprintf(file, "\nSummary:\n%s\n", report->summary_text);
    }
    
    // 写入详细文本
    if (report->detailed_text) {
        fprintf(file, "\nDetails:\n%s\n", report->detailed_text);
    }
    
    // 写入图表数据
    if (report->chart_data) {
        fprintf(file, "\nChart Data:\n%s\n", report->chart_data);
    }
    
    fclose(file);
    
    LOGI("Report saved to %s", path);
    return 0;
}

// 加载监控报告
struct monitor_report* monitor_load_report(const char* path) {
    // 实现从文件加载报告
    // 这是一个简化的实现，实际应用中应该解析完整的报告格式
    LOGI("Report loading not fully implemented");
    return NULL;
}

// 释放监控报告
void monitor_free_report(struct monitor_report* report) {
    if (!report) {
        return;
    }
    
    if (report->summary_text) {
        free(report->summary_text);
    }
    
    if (report->detailed_text) {
        free(report->detailed_text);
    }
    
    if (report->chart_data) {
        free(report->chart_data);
    }
    
    free(report);
}

// 清除监控数据
void monitor_clear_data(monitor_data_type_t type) {
    if (type >= MONITOR_DATA_TYPE_COUNT) {
        LOGE("Invalid data type: %d", type);
        return;
    }
    
    struct monitor_data_buffer* buffer = &g_monitor_state.data_buffers[type];
    buffer->count = 0;
    buffer->head = 0;
    buffer->tail = 0;
    
    LOGI("Cleared data for type %d", type);
}

// 清除所有监控数据
void monitor_clear_all_data(void) {
    for (int i = 0; i < MONITOR_DATA_TYPE_COUNT; i++) {
        monitor_clear_data((monitor_data_type_t)i);
    }
    
    LOGI("Cleared all monitor data");
}

// 注册监控回调
void monitor_register_callback(monitor_callback_t callback, void* user_data) {
    g_monitor_state.callback = callback;
    g_monitor_state.callback_user_data = user_data;
}

// 注销监控回调
void monitor_unregister_callback(monitor_callback_t callback) {
    if (g_monitor_state.callback == callback) {
        g_monitor_state.callback = NULL;
        g_monitor_state.callback_user_data = NULL;
    }
}

// 打印监控状态
void monitor_print_status(void) {
    LOGI("Monitor Status:");
    LOGI("  Enabled: %s", g_monitor_state.settings.enabled ? "yes" : "no");
    LOGI("  Auto Save: %s", g_monitor_state.settings.auto_save ? "yes" : "no");
    LOGI("  Real-time Analysis: %s", g_monitor_state.settings.real_time_analysis ? "yes" : "no");
    LOGI("  Buffer Size: %u", g_monitor_state.settings.buffer_size);
    LOGI("  Sample Interval: %u ms", g_monitor_state.settings.sample_interval_ms);
    LOGI("  Report Interval: %u ms", g_monitor_state.settings.report_interval_ms);
    LOGI("  Save Path: %s", g_monitor_state.settings.save_path);
    LOGI("  Include Charts: %s", g_monitor_state.settings.include_charts ? "yes" : "no");
    LOGI("  Include Detailed Stats: %s", g_monitor_state.settings.include_detailed_stats ? "yes" : "no");
}

// 打印监控统计信息
void monitor_print_statistics(monitor_data_type_t type) {
    if (type >= MONITOR_DATA_TYPE_COUNT) {
        LOGE("Invalid data type: %d", type);
        return;
    }
    
    struct monitor_statistics stats;
    calculate_statistics(&g_monitor_state.data_buffers[type], &stats);
    
    LOGI("Statistics for %s:", get_data_type_name(type));
    LOGI("  Min Value: %.2f %s", stats.min_value, get_data_type_unit(type));
    LOGI("  Max Value: %.2f %s", stats.max_value, get_data_type_unit(type));
    LOGI("  Avg Value: %.2f %s", stats.avg_value, get_data_type_unit(type));
    LOGI("  Median Value: %.2f %s", stats.median_value, get_data_type_unit(type));
    LOGI("  Std Deviation: %.2f %s", stats.std_deviation, get_data_type_unit(type));
    LOGI("  Sample Count: %u", stats.sample_count);
}

// 导出监控数据为CSV
int monitor_export_to_csv(monitor_data_type_t type, const char* path) {
    if (type >= MONITOR_DATA_TYPE_COUNT) {
        LOGE("Invalid data type: %d", type);
        return -1;
    }
    
    if (!path) {
        LOGE("Invalid path");
        return -1;
    }
    
    FILE* file = fopen(path, "w");
    if (!file) {
        LOGE("Failed to open file for writing: %s", path);
        return -1;
    }
    
    // 写入CSV头部
    fprintf(file, "Timestamp,Value\n");
    
    // 写入数据
    struct monitor_data_buffer* buffer = &g_monitor_state.data_buffers[type];
    uint32_t index = buffer->head;
    
    for (uint32_t i = 0; i < buffer->count; i++) {
        struct monitor_data_point* point = &buffer->points[index];
        
        // 转换时间戳为秒
        double timestamp_seconds = point->timestamp / 1000000000.0;
        
        fprintf(file, "%.6f,%.6f\n", timestamp_seconds, point->value);
        
        index = (index + 1) % buffer->capacity;
    }
    
    fclose(file);
    
    LOGI("Data exported to CSV: %s", path);
    return 0;
}

// 导出监控数据为JSON
int monitor_export_to_json(monitor_data_type_t type, const char* path) {
    if (type >= MONITOR_DATA_TYPE_COUNT) {
        LOGE("Invalid data type: %d", type);
        return -1;
    }
    
    if (!path) {
        LOGE("Invalid path");
        return -1;
    }
    
    FILE* file = fopen(path, "w");
    if (!file) {
        LOGE("Failed to open file for writing: %s", path);
        return -1;
    }
    
    // 写入JSON头部
    fprintf(file, "{\n");
    fprintf(file, "  \"type\": \"%s\",\n", get_data_type_name(type));
    fprintf(file, "  \"unit\": \"%s\",\n", get_data_type_unit(type));
    fprintf(file, "  \"data\": [\n");
    
    // 写入数据
    struct monitor_data_buffer* buffer = &g_monitor_state.data_buffers[type];
    uint32_t index = buffer->head;
    
    for (uint32_t i = 0; i < buffer->count; i++) {
        struct monitor_data_point* point = &buffer->points[index];
        
        fprintf(file, "    {\"timestamp\": %llu, \"value\": %.6f}", 
                (unsigned long long)point->timestamp, point->value);
        
        if (i < buffer->count - 1) {
            fprintf(file, ",");
        }
        
        fprintf(file, "\n");
        
        index = (index + 1) % buffer->capacity;
    }
    
    // 写入JSON尾部
    fprintf(file, "  ]\n");
    fprintf(file, "}\n");
    
    fclose(file);
    
    LOGI("Data exported to JSON: %s", path);
    return 0;
}

// 导出所有监控数据为CSV
int monitor_export_all_to_csv(const char* path) {
    if (!path) {
        LOGE("Invalid path");
        return -1;
    }
    
    // 为每种数据类型创建单独的CSV文件
    for (int i = 0; i < MONITOR_DATA_TYPE_COUNT; i++) {
        char file_path[256];
        snprintf(file_path, sizeof(file_path), "%s_%s.csv", path, get_data_type_name((monitor_data_type_t)i));
        
        if (monitor_export_to_csv((monitor_data_type_t)i, file_path) != 0) {
            LOGE("Failed to export data for type %d", i);
            return -1;
        }
    }
    
    LOGI("All data exported to CSV");
    return 0;
}

// 导出所有监控数据为JSON
int monitor_export_all_to_json(const char* path) {
    if (!path) {
        LOGE("Invalid path");
        return -1;
    }
    
    FILE* file = fopen(path, "w");
    if (!file) {
        LOGE("Failed to open file for writing: %s", path);
        return -1;
    }
    
    // 写入JSON头部
    fprintf(file, "{\n");
    
    // 为每种数据类型写入数据
    for (int i = 0; i < MONITOR_DATA_TYPE_COUNT; i++) {
        fprintf(file, "  \"%s\": {\n", get_data_type_name((monitor_data_type_t)i));
        fprintf(file, "    \"unit\": \"%s\",\n", get_data_type_unit((monitor_data_type_t)i));
        fprintf(file, "    \"data\": [\n");
        
        // 写入数据
        struct monitor_data_buffer* buffer = &g_monitor_state.data_buffers[i];
        uint32_t index = buffer->head;
        
        for (uint32_t j = 0; j < buffer->count; j++) {
            struct monitor_data_point* point = &buffer->points[index];
            
            fprintf(file, "      {\"timestamp\": %llu, \"value\": %.6f}", 
                    (unsigned long long)point->timestamp, point->value);
            
            if (j < buffer->count - 1) {
                fprintf(file, ",");
            }
            
            fprintf(file, "\n");
            
            index = (index + 1) % buffer->capacity;
        }
        
        fprintf(file, "    ]\n");
        fprintf(file, "  }");
        
        if (i < MONITOR_DATA_TYPE_COUNT - 1) {
            fprintf(file, ",");
        }
        
        fprintf(file, "\n");
    }
    
    // 写入JSON尾部
    fprintf(file, "}\n");
    
    fclose(file);
    
    LOGI("All data exported to JSON: %s", path);
    return 0;
}

// 内部函数实现
static void sample_data(void) {
    // 采样帧时间
    float frame_time = perf_monitor_get_avg_frame_time();
    monitor_add_data_point(MONITOR_DATA_TYPE_FRAME_TIME, frame_time);
    
    // 采样FPS
    float fps = perf_monitor_get_fps();
    monitor_add_data_point(MONITOR_DATA_TYPE_FPS, fps);
    
    // 采样CPU使用率
    float cpu_usage = perf_monitor_get_counter_average(PERF_COUNTER_CPU_USAGE);
    monitor_add_data_point(MONITOR_DATA_TYPE_CPU_USAGE, cpu_usage);
    
    // 采样GPU使用率
    float gpu_usage = perf_monitor_get_counter_average(PERF_COUNTER_GPU_USAGE);
    monitor_add_data_point(MONITOR_DATA_TYPE_GPU_USAGE, gpu_usage);
    
    // 采样内存使用量
    uint64_t memory_usage = perf_monitor_get_counter(PERF_COUNTER_MEMORY_USAGE);
    monitor_add_data_point(MONITOR_DATA_TYPE_MEMORY_USAGE, (float)memory_usage);
    
    // 采样输入延迟
    float input_latency = perf_monitor_get_counter_average(PERF_COUNTER_INPUT_LATENCY);
    monitor_add_data_point(MONITOR_DATA_TYPE_INPUT_LATENCY, input_latency);
    
    // 采样渲染时间
    float render_time = perf_monitor_get_counter_average(PERF_COUNTER_RENDER_TIME);
    monitor_add_data_point(MONITOR_DATA_TYPE_RENDER_TIME, render_time);
    
    // 采样合成时间
    float composite_time = perf_monitor_get_counter_average(PERF_COUNTER_COMPOSITE_TIME);
    monitor_add_data_point(MONITOR_DATA_TYPE_COMPOSITE_TIME, composite_time);
    
    // 采样呈现时间
    float present_time = perf_monitor_get_counter_average(PERF_COUNTER_PRESENT_TIME);
    monitor_add_data_point(MONITOR_DATA_TYPE_PRESENT_TIME, present_time);
    
    // 采样丢帧数
    float frame_drops = (float)perf_monitor_get_counter(PERF_COUNTER_FRAME_DROPS);
    monitor_add_data_point(MONITOR_DATA_TYPE_FRAME_DROPS, frame_drops);
    
    // 采样热状态
    float thermal_state = (float)perf_monitor_get_thermal_state();
    monitor_add_data_point(MONITOR_DATA_TYPE_THERMAL_STATE, thermal_state);
}

static void generate_reports(void) {
    // 为每种数据类型生成报告
    for (int i = 0; i < MONITOR_DATA_TYPE_COUNT; i++) {
        struct monitor_report* report = monitor_generate_report((monitor_data_type_t)i);
        
        if (report) {
            // 调用回调函数
            if (g_monitor_state.callback) {
                g_monitor_state.callback(report, g_monitor_state.callback_user_data);
            }
            
            // 自动保存报告
            if (g_monitor_state.settings.auto_save) {
                char file_path[256];
                snprintf(file_path, sizeof(file_path), "%s/%s_report.txt", 
                        g_monitor_state.settings.save_path, get_data_type_name((monitor_data_type_t)i));
                
                monitor_save_report(report, file_path);
            }
            
            // 释放报告
            monitor_free_report(report);
        }
    }
}

static void auto_save_reports(void) {
    // 自动保存所有数据为CSV和JSON
    if (g_monitor_state.settings.auto_save) {
        char csv_path[256];
        char json_path[256];
        
        snprintf(csv_path, sizeof(csv_path), "%s/all_data", g_monitor_state.settings.save_path);
        snprintf(json_path, sizeof(json_path), "%s/all_data", g_monitor_state.settings.save_path);
        
        monitor_export_all_to_csv(csv_path);
        monitor_export_all_to_json(json_path);
    }
}

static void init_data_buffer(struct monitor_data_buffer* buffer, uint32_t capacity) {
    if (!buffer || capacity == 0) {
        return;
    }
    
    memset(buffer, 0, sizeof(struct monitor_data_buffer));
    
    buffer->points = malloc(capacity * sizeof(struct monitor_data_point));
    if (!buffer->points) {
        LOGE("Failed to allocate memory for data buffer");
        return;
    }
    
    buffer->capacity = capacity;
}

static void free_data_buffer(struct monitor_data_buffer* buffer) {
    if (!buffer) {
        return;
    }
    
    if (buffer->points) {
        free(buffer->points);
        buffer->points = NULL;
    }
    
    memset(buffer, 0, sizeof(struct monitor_data_buffer));
}

static void add_data_point_to_buffer(struct monitor_data_buffer* buffer, const struct monitor_data_point* point) {
    if (!buffer || !point) {
        return;
    }
    
    // 如果缓冲区已满，覆盖最旧的数据
    if (buffer->count < buffer->capacity) {
        buffer->count++;
    } else {
        buffer->tail = (buffer->tail + 1) % buffer->capacity;
    }
    
    // 添加新数据点
    buffer->points[buffer->head] = *point;
    buffer->head = (buffer->head + 1) % buffer->capacity;
}

static void calculate_statistics(const struct monitor_data_buffer* buffer, struct monitor_statistics* stats) {
    if (!buffer || !stats || buffer->count == 0) {
        return;
    }
    
    memset(stats, 0, sizeof(struct monitor_statistics));
    
    // 复制数据值到临时数组
    float* values = malloc(buffer->count * sizeof(float));
    if (!values) {
        LOGE("Failed to allocate memory for statistics calculation");
        return;
    }
    
    uint32_t index = buffer->tail;
    for (uint32_t i = 0; i < buffer->count; i++) {
        values[i] = buffer->points[index].value;
        index = (index + 1) % buffer->capacity;
    }
    
    // 计算最小值和最大值
    stats->min_value = values[0];
    stats->max_value = values[0];
    float sum = 0.0f;
    
    for (uint32_t i = 0; i < buffer->count; i++) {
        if (values[i] < stats->min_value) {
            stats->min_value = values[i];
        }
        
        if (values[i] > stats->max_value) {
            stats->max_value = values[i];
        }
        
        sum += values[i];
    }
    
    // 计算平均值
    stats->avg_value = sum / buffer->count;
    
    // 计算中位数
    qsort(values, buffer->count, sizeof(float), compare_float);
    
    if (buffer->count % 2 == 0) {
        stats->median_value = (values[buffer->count / 2 - 1] + values[buffer->count / 2]) / 2.0f;
    } else {
        stats->median_value = values[buffer->count / 2];
    }
    
    // 计算标准差
    float variance = 0.0f;
    for (uint32_t i = 0; i < buffer->count; i++) {
        float diff = values[i] - stats->avg_value;
        variance += diff * diff;
    }
    
    stats->std_deviation = sqrtf(variance / buffer->count);
    
    // 设置样本数量和时间范围
    stats->sample_count = buffer->count;
    stats->first_timestamp = buffer->points[buffer->tail].timestamp;
    stats->last_timestamp = buffer->points[(buffer->head + buffer->capacity - 1) % buffer->capacity].timestamp;
    
    free(values);
}

static int compare_float(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    
    if (fa < fb) {
        return -1;
    } else if (fa > fb) {
        return 1;
    } else {
        return 0;
    }
}

static char* generate_summary_text(const struct monitor_statistics* stats, monitor_data_type_t type) {
    if (!stats) {
        return NULL;
    }
    
    char* text = malloc(1024);
    if (!text) {
        return NULL;
    }
    
    snprintf(text, 1024, 
             "Summary for %s:\n"
             "  Average: %.2f %s\n"
             "  Range: %.2f - %.2f %s\n"
             "  Standard Deviation: %.2f %s\n"
             "  Samples: %u\n",
             get_data_type_name(type),
             stats->avg_value, get_data_type_unit(type),
             stats->min_value, stats->max_value, get_data_type_unit(type),
             stats->std_deviation, get_data_type_unit(type),
             stats->sample_count);
    
    return text;
}

static char* generate_detailed_text(const struct monitor_statistics* stats, monitor_data_type_t type) {
    if (!stats) {
        return NULL;
    }
    
    char* text = malloc(2048);
    if (!text) {
        return NULL;
    }
    
    time_t start_time = stats->first_timestamp / 1000000000ULL;
    time_t end_time = stats->last_timestamp / 1000000000ULL;
    
    snprintf(text, 2048,
             "Detailed Analysis for %s:\n"
             "  Min Value: %.2f %s\n"
             "  Max Value: %.2f %s\n"
             "  Average Value: %.2f %s\n"
             "  Median Value: %.2f %s\n"
             "  Standard Deviation: %.2f %s\n"
             "  Sample Count: %u\n"
             "  Time Range: %s to %s\n"
             "  Coefficient of Variation: %.2f%%\n",
             get_data_type_name(type),
             stats->min_value, get_data_type_unit(type),
             stats->max_value, get_data_type_unit(type),
             stats->avg_value, get_data_type_unit(type),
             stats->median_value, get_data_type_unit(type),
             stats->std_deviation, get_data_type_unit(type),
             stats->sample_count,
             ctime(&start_time), ctime(&end_time),
             stats->avg_value > 0 ? (stats->std_deviation / stats->avg_value * 100.0f) : 0.0f);
    
    return text;
}

static char* generate_chart_data(const struct monitor_data_buffer* buffer) {
    if (!buffer || buffer->count == 0) {
        return NULL;
    }
    
    // 计算所需内存
    size_t required_size = buffer->count * 100 + 1000; // 每个点约100字节，加上一些额外空间
    char* json = malloc(required_size);
    if (!json) {
        return NULL;
    }
    
    // 生成JSON格式的图表数据
    strcpy(json, "{\"data\":[");
    
    uint32_t index = buffer->tail;
    for (uint32_t i = 0; i < buffer->count; i++) {
        struct monitor_data_point* point = &buffer->points[index];
        
        char point_str[100];
        snprintf(point_str, sizeof(point_str), "{\"x\":%llu,\"y\":%.6f}", 
                (unsigned long long)point->timestamp, point->value);
        
        strcat(json, point_str);
        
        if (i < buffer->count - 1) {
            strcat(json, ",");
        }
        
        index = (index + 1) % buffer->capacity;
    }
    
    strcat(json, "]}");
    
    return json;
}

static uint64_t get_current_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static const char* get_data_type_name(monitor_data_type_t type) {
    switch (type) {
        case MONITOR_DATA_TYPE_FRAME_TIME:
            return "FrameTime";
        case MONITOR_DATA_TYPE_FPS:
            return "FPS";
        case MONITOR_DATA_TYPE_CPU_USAGE:
            return "CPUUsage";
        case MONITOR_DATA_TYPE_GPU_USAGE:
            return "GPUUsage";
        case MONITOR_DATA_TYPE_MEMORY_USAGE:
            return "MemoryUsage";
        case MONITOR_DATA_TYPE_INPUT_LATENCY:
            return "InputLatency";
        case MONITOR_DATA_TYPE_RENDER_TIME:
            return "RenderTime";
        case MONITOR_DATA_TYPE_COMPOSITE_TIME:
            return "CompositeTime";
        case MONITOR_DATA_TYPE_PRESENT_TIME:
            return "PresentTime";
        case MONITOR_DATA_TYPE_FRAME_DROPS:
            return "FrameDrops";
        case MONITOR_DATA_TYPE_THERMAL_STATE:
            return "ThermalState";
        default:
            return "Unknown";
    }
}

static const char* get_data_type_unit(monitor_data_type_t type) {
    switch (type) {
        case MONITOR_DATA_TYPE_FRAME_TIME:
        case MONITOR_DATA_TYPE_RENDER_TIME:
        case MONITOR_DATA_TYPE_COMPOSITE_TIME:
        case MONITOR_DATA_TYPE_PRESENT_TIME:
        case MONITOR_DATA_TYPE_INPUT_LATENCY:
            return "ms";
        case MONITOR_DATA_TYPE_FPS:
            return "fps";
        case MONITOR_DATA_TYPE_CPU_USAGE:
        case MONITOR_DATA_TYPE_GPU_USAGE:
            return "%";
        case MONITOR_DATA_TYPE_MEMORY_USAGE:
            return "bytes";
        case MONITOR_DATA_TYPE_FRAME_DROPS:
            return "frames";
        case MONITOR_DATA_TYPE_THERMAL_STATE:
            return "state";
        default:
            return "";
    }
}