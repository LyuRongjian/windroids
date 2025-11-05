/*
 * WinDroids Compositor
 * Vulkan Performance Monitoring Header
 */

#ifndef COMPOSITOR_VULKAN_PERF_H
#define COMPOSITOR_VULKAN_PERF_H

#include "compositor_vulkan_core.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 性能监控统计数据
typedef struct {
    uint64_t frame_count;              // 帧计数
    double avg_frame_time;             // 平均帧时间(ms)
    double min_frame_time;             // 最小帧时间(ms)
    double max_frame_time;             // 最大帧时间(ms)
    uint32_t draw_calls_per_frame;     // 每帧绘制调用次数
    uint32_t batch_count_per_frame;    // 每帧批次数量
    uint32_t texture_switches_per_frame; // 每帧纹理切换次数
    size_t total_memory_usage;         // 总内存使用量(bytes)
    size_t peak_memory_usage;          // 峰值内存使用量(bytes)
    uint32_t fps;                      // 当前FPS
    uint64_t start_time;               // 监控开始时间
    uint64_t last_update_time;         // 上次更新时间
} VulkanPerfStats;

// 初始化性能监控
int init_vulkan_performance_monitoring(VulkanPerfStats* perf_stats);

// 更新性能统计
void update_vulkan_performance_stats(VulkanPerfStats* perf_stats, uint64_t frame_time);

// 重置性能统计
void reset_vulkan_performance_stats(VulkanPerfStats* perf_stats);

// 获取当前FPS
uint32_t get_current_fps(const VulkanPerfStats* perf_stats);

// 获取平均帧时间
double get_average_frame_time(const VulkanPerfStats* perf_stats);

// 获取内存使用统计
void get_memory_usage_stats(const VulkanPerfStats* perf_stats, size_t* total, size_t* peak);

// 记录绘制调用
void record_draw_call(VulkanPerfStats* perf_stats);

// 记录批次
void record_batch(VulkanPerfStats* perf_stats);

// 记录纹理切换
void record_texture_switch(VulkanPerfStats* perf_stats);

// 打印性能统计信息
void print_vulkan_performance_stats(const VulkanPerfStats* perf_stats);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_VULKAN_PERF_H