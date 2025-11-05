/*
 * WinDroids Compositor - Performance Monitoring
 * Implementation of advanced performance tracking and analysis
 */

#include "compositor_perf.h"
#include "compositor_utils.h"
#include "compositor_render.h"
#include "compositor_dirty.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// 全局状态引用
static CompositorState* g_state = NULL;

// 性能统计数据
static CompositorPerformanceStats g_perf_stats;

// 性能测量时间戳
static int64_t g_frame_start_time = 0;
static int64_t g_render_start_time = 0;
static int64_t g_input_start_time = 0;

// 配置标志
static bool g_perf_enabled = true;

// 设置合成器状态引用（内部使用）
void compositor_perf_set_state(CompositorState* state) {
    g_state = state;
}

// 初始化性能监控系统
int compositor_perf_init(void) {
    if (!g_state) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 初始化性能统计数据
    memset(&g_perf_stats, 0, sizeof(CompositorPerformanceStats));
    
    // 设置初始最小值为较大值
    g_perf_stats.min_fps = 1000.0f;
    g_perf_stats.min_frame_time = 1000.0f;
    g_perf_stats.min_render_time = 1000.0f;
    g_perf_stats.min_input_time = 1000.0f;
    
    // 设置警告阈值
    g_perf_stats.low_fps_warning = false;
    g_perf_stats.high_memory_warning = false;
    g_perf_stats.high_cpu_warning = false;
    
    g_perf_enabled = g_state->config.enable_performance_monitoring;
    
    log_message(COMPOSITOR_LOG_INFO, "Performance monitoring system initialized");
    return COMPOSITOR_OK;
}

// 清理性能监控系统
void compositor_perf_cleanup(void) {
    if (g_state) {
        log_message(COMPOSITOR_LOG_INFO, "Performance monitoring system cleaned up");
    }
}

// 开始帧性能测量
void compositor_perf_start_frame(void) {
    if (!g_perf_enabled || !g_state) return;
    
    g_frame_start_time = get_current_time_ms();
}

// 结束帧性能测量
void compositor_perf_end_frame(void) {
    if (!g_perf_enabled || !g_state || g_frame_start_time == 0) return;
    
    int64_t current_time = get_current_time_ms();
    float frame_time = (float)(current_time - g_frame_start_time);
    
    // 更新帧时间统计
    g_perf_stats.current_frame_time = frame_time;
    g_perf_stats.min_frame_time = MIN(g_perf_stats.min_frame_time, frame_time);
    g_perf_stats.max_frame_time = MAX(g_perf_stats.max_frame_time, frame_time);
    
    // 更新FPS
    if (frame_time > 0) {
        float fps = 1000.0f / frame_time;
        g_perf_stats.current_fps = fps;
        g_perf_stats.min_fps = MIN(g_perf_stats.min_fps, fps);
        g_perf_stats.max_fps = MAX(g_perf_stats.max_fps, fps);
    }
    
    // 重置开始时间
    g_frame_start_time = 0;
}

// 开始渲染阶段性能测量
void compositor_perf_start_render(void) {
    if (!g_perf_enabled || !g_state) return;
    
    g_render_start_time = get_current_time_ms();
}

// 结束渲染阶段性能测量
void compositor_perf_end_render(void) {
    if (!g_perf_enabled || !g_state || g_render_start_time == 0) return;
    
    float render_time = (float)(get_current_time_ms() - g_render_start_time);
    
    // 更新渲染时间统计
    g_perf_stats.current_render_time = render_time;
    g_perf_stats.min_render_time = MIN(g_perf_stats.min_render_time, render_time);
    g_perf_stats.max_render_time = MAX(g_perf_stats.max_render_time, render_time);
    
    // 重置开始时间
    g_render_start_time = 0;
}

// 开始输入处理阶段性能测量
void compositor_perf_start_input(void) {
    if (!g_perf_enabled || !g_state) return;
    
    g_input_start_time = get_current_time_ms();
}

// 结束输入处理阶段性能测量
void compositor_perf_end_input(void) {
    if (!g_perf_enabled || !g_state || g_input_start_time == 0) return;
    
    float input_time = (float)(get_current_time_ms() - g_input_start_time);
    
    // 更新输入处理时间统计
    g_perf_stats.current_input_time = input_time;
    g_perf_stats.min_input_time = MIN(g_perf_stats.min_input_time, input_time);
    g_perf_stats.max_input_time = MAX(g_perf_stats.max_input_time, input_time);
    
    // 重置开始时间
    g_input_start_time = 0;
}

// 更新性能统计
void compositor_perf_update_stats(void) {
    if (!g_perf_enabled || !g_state) return;
    
    // 更新平均统计数据（使用指数移动平均）
    static int frames_processed = 0;
    frames_processed++;
    
    float alpha = 0.9f; // 平滑因子
    
    // 更新FPS平均值
    g_perf_stats.avg_fps = (frames_processed == 1) ? 
                           g_perf_stats.current_fps : 
                           alpha * g_perf_stats.avg_fps + (1 - alpha) * g_perf_stats.current_fps;
    
    // 更新帧时间平均值
    g_perf_stats.avg_frame_time = (frames_processed == 1) ? 
                                 g_perf_stats.current_frame_time : 
                                 alpha * g_perf_stats.avg_frame_time + (1 - alpha) * g_perf_stats.current_frame_time;
    
    // 更新渲染时间平均值
    g_perf_stats.avg_render_time = (frames_processed == 1) ? 
                                  g_perf_stats.current_render_time : 
                                  alpha * g_perf_stats.avg_render_time + (1 - alpha) * g_perf_stats.current_render_time;
    
    // 更新输入处理时间平均值
    g_perf_stats.avg_input_time = (frames_processed == 1) ? 
                                 g_perf_stats.current_input_time : 
                                 alpha * g_perf_stats.avg_input_time + (1 - alpha) * g_perf_stats.current_input_time;
    
    // 更新内存使用统计
    g_perf_stats.current_memory_usage = g_state->total_allocated / 1024; // 转换为KB
    g_perf_stats.peak_memory_usage = g_state->peak_allocated / 1024; // 转换为KB
    
    // 更新窗口统计
    g_perf_stats.total_windows = g_state->xwayland_state.window_count + g_state->wayland_state.window_count;
    g_perf_stats.active_windows = (g_state->active_window != NULL) ? 1 : 0;
    
    // 更新脏区域统计
    g_perf_stats.dirty_rect_count = g_state->dirty_rect_count;
    if (g_state->use_dirty_rect_optimization && g_state->dirty_rect_count > 0) {
        int total_dirty_area = 0;
        for (int i = 0; i < g_state->dirty_rect_count; i++) {
            total_dirty_area += g_state->dirty_rects[i].width * g_state->dirty_rects[i].height;
        }
        int screen_area = g_state->width * g_state->height;
        g_perf_stats.screen_coverage_percent = (screen_area > 0) ? 
                                              (float)total_dirty_area * 100.0f / screen_area : 0.0f;
    } else {
        g_perf_stats.screen_coverage_percent = 100.0f; // 全屏渲染
    }
    
    // 更新性能警告标志
    g_perf_stats.low_fps_warning = (g_perf_stats.avg_fps < 30.0f);
    g_perf_stats.high_memory_warning = (g_perf_stats.current_memory_usage > 50 * 1024); // 50MB
    g_perf_stats.high_cpu_warning = (g_perf_stats.avg_frame_time > 50.0f); // 超过50ms
    
    // 记录性能警告
    if (g_state->config.debug_mode) {
        if (g_perf_stats.low_fps_warning) {
            compositor_perf_record_warning("Low FPS detected");
        }
        if (g_perf_stats.high_memory_warning) {
            compositor_perf_record_warning("High memory usage detected");
        }
        if (g_perf_stats.high_cpu_warning) {
            compositor_perf_record_warning("High CPU usage detected");
        }
    }
}

// 获取性能统计数据
const CompositorPerformanceStats* compositor_perf_get_stats(void) {
    return &g_perf_stats;
}

// 记录性能警告
void compositor_perf_record_warning(const char* warning_message) {
    if (!g_perf_enabled || !warning_message) return;
    
    log_message(COMPOSITOR_LOG_WARN, "Performance warning: %s", warning_message);
}

// 生成性能报告（返回动态分配的字符串，需要调用者释放）
char* compositor_perf_generate_report(void) {
    if (!g_perf_enabled || !g_state) return NULL;
    
    // 分配足够的空间
    char* report = (char*)safe_malloc(2048);
    if (!report) return NULL;
    
    int written = snprintf(report, 2048, 
        "===== WinDroids Compositor Performance Report =====\n"\n"
        "FPS: %.1f (min: %.1f, max: %.1f, avg: %.1f)\n"\n"
        "Frame Time: %.2fms (min: %.2fms, max: %.2fms, avg: %.2fms)\n"\n"
        "Render Time: %.2fms (min: %.2fms, max: %.2fms, avg: %.2fms)\n"\n"
        "Input Time: %.2fms (min: %.2fms, max: %.2fms, avg: %.2fms)\n"\n"
        "Memory Usage: %zu KB (peak: %zu KB)\n"\n"
        "Windows: %d total, %d active\n"\n"
        "Render Coverage: %.1f%%, Dirty Rects: %d\n"\n"
        "Warnings: %s %s %s\n"\n"
        "=================================================\n",
        g_perf_stats.current_fps, g_perf_stats.min_fps, g_perf_stats.max_fps, g_perf_stats.avg_fps,
        g_perf_stats.current_frame_time, g_perf_stats.min_frame_time, g_perf_stats.max_frame_time, g_perf_stats.avg_frame_time,
        g_perf_stats.current_render_time, g_perf_stats.min_render_time, g_perf_stats.max_render_time, g_perf_stats.avg_render_time,
        g_perf_stats.current_input_time, g_perf_stats.min_input_time, g_perf_stats.max_input_time, g_perf_stats.avg_input_time,
        g_perf_stats.current_memory_usage, g_perf_stats.peak_memory_usage,
        g_perf_stats.total_windows, g_perf_stats.active_windows,
        g_perf_stats.screen_coverage_percent, g_perf_stats.dirty_rect_count,
        g_perf_stats.low_fps_warning ? "Low FPS" : "",
        g_perf_stats.high_memory_warning ? "High Memory" : "",
        g_perf_stats.high_cpu_warning ? "High CPU" : ""
    );
    
    // 确保字符串以null结尾
    if (written >= 2048) {
        report[2047] = '\0';
    }
    
    return report;
}

// 重置性能统计
void compositor_perf_reset(void) {
    // 保存当前的最小值状态，只重置累积统计
    float min_fps = g_perf_stats.min_fps;
    float min_frame_time = g_perf_stats.min_frame_time;
    float min_render_time = g_perf_stats.min_render_time;
    float min_input_time = g_perf_stats.min_input_time;
    size_t peak_memory = g_perf_stats.peak_memory_usage;
    
    // 重置统计数据
    memset(&g_perf_stats, 0, sizeof(CompositorPerformanceStats));
    
    // 恢复最小值
    g_perf_stats.min_fps = min_fps;
    g_perf_stats.min_frame_time = min_frame_time;
    g_perf_stats.min_render_time = min_render_time;
    g_perf_stats.min_input_time = min_input_time;
    g_perf_stats.peak_memory_usage = peak_memory;
    
    // 重置时间戳
    g_frame_start_time = 0;
    g_render_start_time = 0;
    g_input_start_time = 0;
    
    log_message(COMPOSITOR_LOG_INFO, "Performance statistics reset");
}

// 启用/禁用性能监控
void compositor_perf_set_enabled(bool enabled) {
    g_perf_enabled = enabled;
    log_message(COMPOSITOR_LOG_INFO, "Performance monitoring %s", enabled ? "enabled" : "disabled");
}