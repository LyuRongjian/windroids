#ifndef COMPOSITOR_MONITOR_H
#define COMPOSITOR_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 监控数据类型
typedef enum {
    MONITOR_DATA_TYPE_FRAME_TIME = 0,
    MONITOR_DATA_TYPE_FPS,
    MONITOR_DATA_TYPE_CPU_USAGE,
    MONITOR_DATA_TYPE_GPU_USAGE,
    MONITOR_DATA_TYPE_MEMORY_USAGE,
    MONITOR_DATA_TYPE_INPUT_LATENCY,
    MONITOR_DATA_TYPE_RENDER_TIME,
    MONITOR_DATA_TYPE_COMPOSITE_TIME,
    MONITOR_DATA_TYPE_PRESENT_TIME,
    MONITOR_DATA_TYPE_FRAME_DROPS,
    MONITOR_DATA_TYPE_THERMAL_STATE,
    MONITOR_DATA_TYPE_COUNT
} monitor_data_type_t;

// 监控数据点
struct monitor_data_point {
    uint64_t timestamp;             // 时间戳（纳秒）
    monitor_data_type_t type;       // 数据类型
    float value;                    // 数据值
};

// 监控数据缓冲区
struct monitor_data_buffer {
    struct monitor_data_point* points;  // 数据点数组
    uint32_t capacity;                  // 缓冲区容量
    uint32_t count;                     // 当前数据点数量
    uint32_t head;                      // 头索引（环形缓冲区）
    uint32_t tail;                      // 尾索引（环形缓冲区）
};

// 监控统计信息
struct monitor_statistics {
    float min_value;               // 最小值
    float max_value;               // 最大值
    float avg_value;               // 平均值
    float median_value;            // 中位数
    float std_deviation;           // 标准差
    uint32_t sample_count;         // 样本数量
    uint64_t first_timestamp;      // 第一个样本的时间戳
    uint64_t last_timestamp;       // 最后一个样本的时间戳
};

// 监控报告
struct monitor_report {
    monitor_data_type_t type;      // 报告类型
    struct monitor_statistics stats;  // 统计信息
    char* summary_text;            // 摘要文本
    char* detailed_text;           // 详细文本
    char* chart_data;              // 图表数据（JSON格式）
};

// 监控设置
struct monitor_settings {
    bool enabled;                  // 是否启用监控
    bool auto_save;                // 是否自动保存报告
    bool real_time_analysis;       // 是否启用实时分析
    uint32_t buffer_size;          // 缓冲区大小
    uint32_t sample_interval_ms;   // 采样间隔（毫秒）
    uint32_t report_interval_ms;   // 报告生成间隔（毫秒）
    char* save_path;               // 保存路径
    bool include_charts;           // 是否包含图表
    bool include_detailed_stats;   // 是否包含详细统计
};

// 监控回调函数类型
typedef void (*monitor_callback_t)(const struct monitor_report* report, void* user_data);

// 监控接口函数

// 初始化监控模块
int monitor_init(void);

// 销毁监控模块
void monitor_destroy(void);

// 更新监控模块（每帧调用）
void monitor_update(void);

// 启用/禁用监控
void monitor_set_enabled(bool enabled);

// 检查监控是否启用
bool monitor_is_enabled(void);

// 设置监控设置
void monitor_set_settings(const struct monitor_settings* settings);

// 获取监控设置
void monitor_get_settings(struct monitor_settings* settings);

// 启用/禁用自动保存
void monitor_set_auto_save_enabled(bool enabled);

// 检查自动保存是否启用
bool monitor_is_auto_save_enabled(void);

// 设置采样间隔
void monitor_set_sample_interval(uint32_t interval_ms);

// 获取采样间隔
uint32_t monitor_get_sample_interval(void);

// 设置报告生成间隔
void monitor_set_report_interval(uint32_t interval_ms);

// 获取报告生成间隔
uint32_t monitor_get_report_interval(void);

// 设置保存路径
void monitor_set_save_path(const char* path);

// 获取保存路径
const char* monitor_get_save_path(void);

// 启用/禁用实时分析
void monitor_set_real_time_analysis_enabled(bool enabled);

// 检查实时分析是否启用
bool monitor_is_real_time_analysis_enabled(void);

// 启用/禁用图表生成
void monitor_set_charts_enabled(bool enabled);

// 检查图表生成是否启用
bool monitor_is_charts_enabled(void);

// 启用/禁用详细统计
void monitor_set_detailed_stats_enabled(bool enabled);

// 检查详细统计是否启用
bool monitor_is_detailed_stats_enabled(void);

// 添加监控数据点
void monitor_add_data_point(monitor_data_type_t type, float value);

// 获取监控数据缓冲区
void monitor_get_data_buffer(monitor_data_type_t type, struct monitor_data_buffer* buffer);

// 获取监控统计信息
void monitor_get_statistics(monitor_data_type_t type, struct monitor_statistics* stats);

// 生成监控报告
struct monitor_report* monitor_generate_report(monitor_data_type_t type);

// 保存监控报告
int monitor_save_report(const struct monitor_report* report, const char* path);

// 加载监控报告
struct monitor_report* monitor_load_report(const char* path);

// 释放监控报告
void monitor_free_report(struct monitor_report* report);

// 清除监控数据
void monitor_clear_data(monitor_data_type_t type);

// 清除所有监控数据
void monitor_clear_all_data(void);

// 注册监控回调
void monitor_register_callback(monitor_callback_t callback, void* user_data);

// 注销监控回调
void monitor_unregister_callback(monitor_callback_t callback);

// 打印监控状态
void monitor_print_status(void);

// 打印监控统计信息
void monitor_print_statistics(monitor_data_type_t type);

// 导出监控数据为CSV
int monitor_export_to_csv(monitor_data_type_t type, const char* path);

// 导出监控数据为JSON
int monitor_export_to_json(monitor_data_type_t type, const char* path);

// 导出所有监控数据为CSV
int monitor_export_all_to_csv(const char* path);

// 导出所有监控数据为JSON
int monitor_export_all_to_json(const char* path);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_MONITOR_H