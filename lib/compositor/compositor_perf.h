/*
 * WinDroids Compositor - Performance Monitoring
 * Advanced performance tracking, analysis and reporting system
 */

#ifndef COMPOSITOR_PERF_H
#define COMPOSITOR_PERF_H

#include "compositor.h"

// 性能指标结构体
typedef struct {
    // FPS统计
    float current_fps;
    float min_fps;
    float max_fps;
    float avg_fps;
    
    // 帧时间统计（毫秒）
    float current_frame_time;
    float min_frame_time;
    float max_frame_time;
    float avg_frame_time;
    
    // 渲染时间统计（毫秒）
    float current_render_time;
    float min_render_time;
    float max_render_time;
    float avg_render_time;
    
    // 输入处理时间统计（毫秒）
    float current_input_time;
    float min_input_time;
    float max_input_time;
    float avg_input_time;
    
    // 内存使用统计（KB）
    size_t current_memory_usage;
    size_t peak_memory_usage;
    
    // 窗口统计
    int active_windows;
    int total_windows;
    
    // 渲染区域统计
    int dirty_rect_count;
    float screen_coverage_percent; // 脏区域占屏幕的百分比
    
    // 性能警告标志
    bool low_fps_warning;
    bool high_memory_warning;
    bool high_cpu_warning;
} CompositorPerformanceStats;

// 设置合成器状态引用（内部使用）
void compositor_perf_set_state(CompositorState* state);

// 初始化性能监控系统
int compositor_perf_init(void);

// 清理性能监控系统
void compositor_perf_cleanup(void);

// 开始帧性能测量
void compositor_perf_start_frame(void);

// 结束帧性能测量
void compositor_perf_end_frame(void);

// 开始渲染阶段性能测量
void compositor_perf_start_render(void);

// 结束渲染阶段性能测量
void compositor_perf_end_render(void);

// 开始输入处理阶段性能测量
void compositor_perf_start_input(void);

// 结束输入处理阶段性能测量
void compositor_perf_end_input(void);

// 更新性能统计
void compositor_perf_update_stats(void);

// 获取性能统计数据
const CompositorPerformanceStats* compositor_perf_get_stats(void);

// 记录性能警告
void compositor_perf_record_warning(const char* warning_message);

// 生成性能报告（返回动态分配的字符串，需要调用者释放）
char* compositor_perf_generate_report(void);

// 重置性能统计
void compositor_perf_reset(void);

// 启用/禁用性能监控
void compositor_perf_set_enabled(bool enabled);

#endif // COMPOSITOR_PERF_H