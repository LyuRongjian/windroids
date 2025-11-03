#ifndef COMPOSITOR_CONFIG_H
#define COMPOSITOR_CONFIG_H

#include <stdint.h>

// 配置参数结构
typedef struct {
    // Xwayland 配置
    int enable_xwayland;           // 是否启用 Xwayland
    const char* xwayland_path;     // Xwayland 可执行文件路径，NULL 则使用默认值
    int xwayland_display_number;   // Xwayland 显示编号
    
    // 渲染配置
    int enable_vsync;              // 是否启用垂直同步
    int preferred_refresh_rate;    // 首选刷新率，0 则使用默认值
    int max_swapchain_images;      // 最大交换链图像数量
    float initial_scale;           // 初始缩放比例
    int render_quality;            // 渲染质量（0-100）
    bool enable_animations;        // 是否启用动画效果
    bool enable_alpha_compositing; // 是否启用alpha合成
    
    // 窗口管理
    int default_window_width;      // 默认窗口宽度
    int default_window_height;     // 默认窗口高度
    int enable_window_decoration;  // 是否启用窗口装饰（边框、标题栏等）
    int max_windows;               // 最大窗口数量
    
    // 调试选项
    int log_level;                 // 日志级别 (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG)
    int enable_tracing;            // 是否启用跟踪
    int enable_perf_monitoring;    // 是否启用性能监控
    int enable_debug_logging;      // 是否启用调试日志
    bool debug_mode;               // 调试模式
    
    // 其他配置
    float background_color[3];     // 背景颜色 RGB
    bool use_hardware_acceleration; // 是否使用硬件加速
    int refresh_rate;              // 刷新率
} CompositorConfig;

// 获取默认配置
CompositorConfig* compositor_get_default_config(void);

// 设置日志级别
// - level: 日志级别 (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG)
void compositor_set_log_level(int level);

#endif // COMPOSITOR_CONFIG_H