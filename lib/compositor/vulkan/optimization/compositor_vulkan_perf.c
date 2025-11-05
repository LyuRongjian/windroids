/*
 * WinDroids Compositor
 * Vulkan Performance Monitoring Implementation
 */

#include "compositor_vulkan_perf.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdio.h>

// 初始化性能监控
int init_vulkan_performance_monitoring(VulkanPerfStats* perf_stats) {
    if (!perf_stats) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameters for init_vulkan_performance_monitoring");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 初始化性能统计数据
    memset(perf_stats, 0, sizeof(VulkanPerfStats));
    perf_stats->min_frame_time = 1000000.0; // 设置一个大的初始值
    perf_stats->start_time = get_current_time_ms();
    perf_stats->last_update_time = perf_stats->start_time;
    
    log_message(COMPOSITOR_LOG_INFO, "Vulkan performance monitoring initialized successfully");
    return COMPOSITOR_OK;
}

// 更新性能统计
void update_vulkan_performance_stats(VulkanPerfStats* perf_stats, uint64_t frame_time) {
    if (!perf_stats) {
        return;
    }
    
    // 更新帧计数
    perf_stats->frame_count++;
    
    // 更新帧时间统计
    double frame_time_ms = (double)frame_time / 1000000.0; // 纳秒转毫秒
    perf_stats->avg_frame_time = (perf_stats->avg_frame_time * (perf_stats->frame_count - 1) + frame_time_ms) / perf_stats->frame_count;
    
    if (frame_time_ms < perf_stats->min_frame_time) {
        perf_stats->min_frame_time = frame_time_ms;
    }
    
    if (frame_time_ms > perf_stats->max_frame_time) {
        perf_stats->max_frame_time = frame_time_ms;
    }
    
    // 计算FPS (每秒帧数)
    uint64_t elapsed_time = get_current_time_ms() - perf_stats->start_time;
    if (elapsed_time > 0) {
        perf_stats->fps = (uint32_t)(perf_stats->frame_count * 1000 / elapsed_time);
    }
    
    // 更新内存使用统计
    if (perf_stats->total_memory_usage > perf_stats->peak_memory_usage) {
        perf_stats->peak_memory_usage = perf_stats->total_memory_usage;
    }
    
    // 重置每帧统计
    perf_stats->draw_calls_per_frame = 0;
    perf_stats->batch_count_per_frame = 0;
    perf_stats->texture_switches_per_frame = 0;
    
    perf_stats->last_update_time = get_current_time_ms();
}

// 重置性能统计
void reset_vulkan_performance_stats(VulkanPerfStats* perf_stats) {
    if (!perf_stats) {
        return;
    }
    
    uint64_t start_time = perf_stats->start_time;
    memset(perf_stats, 0, sizeof(VulkanPerfStats));
    perf_stats->min_frame_time = 1000000.0; // 设置一个大的初始值
    perf_stats->start_time = start_time;
    perf_stats->last_update_time = get_current_time_ms();
}

// 获取当前FPS
uint32_t get_current_fps(const VulkanPerfStats* perf_stats) {
    return perf_stats ? perf_stats->fps : 0;
}

// 获取平均帧时间
double get_average_frame_time(const VulkanPerfStats* perf_stats) {
    return perf_stats ? perf_stats->avg_frame_time : 0.0;
}

// 获取内存使用统计
void get_memory_usage_stats(const VulkanPerfStats* perf_stats, size_t* total, size_t* peak) {
    if (!perf_stats) {
        if (total) *total = 0;
        if (peak) *peak = 0;
        return;
    }
    
    if (total) *total = perf_stats->total_memory_usage;
    if (peak) *peak = perf_stats->peak_memory_usage;
}

// 记录绘制调用
void record_draw_call(VulkanPerfStats* perf_stats) {
    if (perf_stats) {
        perf_stats->draw_calls_per_frame++;
    }
}

// 记录批次
void record_batch(VulkanPerfStats* perf_stats) {
    if (perf_stats) {
        perf_stats->batch_count_per_frame++;
    }
}

// 记录纹理切换
void record_texture_switch(VulkanPerfStats* perf_stats) {
    if (perf_stats) {
        perf_stats->texture_switches_per_frame++;
    }
}

// 打印性能统计信息
void print_vulkan_performance_stats(const VulkanPerfStats* perf_stats) {
    if (!perf_stats) {
        return;
    }
    
    log_message(COMPOSITOR_LOG_INFO, 
               "Performance Stats: FPS=%u, AvgFrameTime=%.2fms, MinFrameTime=%.2fms, MaxFrameTime=%.2fms",
               perf_stats->fps, 
               perf_stats->avg_frame_time,
               perf_stats->min_frame_time,
               perf_stats->max_frame_time);
    
    log_message(COMPOSITOR_LOG_INFO,
               "Per Frame: DrawCalls=%u, Batches=%u, TextureSwitches=%u",
               perf_stats->draw_calls_per_frame,
               perf_stats->batch_count_per_frame,
               perf_stats->texture_switches_per_frame);
    
    log_message(COMPOSITOR_LOG_INFO,
               "Memory: Total=%zu bytes, Peak=%zu bytes",
               perf_stats->total_memory_usage,
               perf_stats->peak_memory_usage);
}