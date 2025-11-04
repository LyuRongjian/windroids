#ifndef COMPOSITOR_CONFIG_H
#define COMPOSITOR_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// 窗口尺寸限制
#define WINDOW_MIN_WIDTH 200
#define WINDOW_MIN_HEIGHT 100

// 配置参数结构
typedef struct {
    // Xwayland 配置
    bool enable_xwayland;          // 是否启用 Xwayland
    const char* xwayland_path;     // Xwayland 可执行文件路径，NULL 则使用默认值
    int32_t xwayland_display_number; // Xwayland 显示编号
    bool xwayland_force_fullscreen; // 是否强制 Xwayland 窗口全屏
    
    // 渲染配置
    bool enable_vsync;             // 是否启用垂直同步
    int32_t preferred_refresh_rate; // 首选刷新率，0 则使用默认值
    int32_t max_swapchain_images;  // 最大交换链图像数量
    float initial_scale;           // 初始缩放比例
    int32_t render_quality;        // 渲染质量（0-100）
    bool enable_animations;        // 是否启用动画效果
    bool enable_alpha_compositing; // 是否启用alpha合成
    bool enable_dirty_rects;       // 是否启用脏区域优化
    int32_t max_dirty_rects;       // 最大脏区域数量
    bool enable_scissor_test;      // 是否启用剪裁测试
    
    // 窗口管理
    int32_t default_window_width;  // 默认窗口宽度
    int32_t default_window_height; // 默认窗口高度
    bool enable_window_decoration; // 是否启用窗口装饰（边框、标题栏等）
    int32_t max_windows;           // 最大窗口数量
    bool enable_window_cycling;    // 是否启用窗口切换快捷键
    int32_t window_border_width;   // 窗口边框宽度
    int32_t window_titlebar_height;// 窗口标题栏高度
    bool enable_window_shadows;    // 是否启用窗口阴影
    float window_shadow_opacity;   // 窗口阴影透明度
    
    // 内存管理
    size_t texture_cache_size_mb;  // 纹理缓存大小（MB）
    int32_t texture_cache_max_items; // 纹理缓存最大项目数
    bool enable_memory_tracking;   // 是否启用内存跟踪
    size_t max_memory_usage_mb;    // 最大内存使用量（MB）
    bool enable_memory_compression; // 是否启用内存压缩
    
    // 输入设备
    bool enable_mouse;             // 是否启用鼠标
    bool enable_keyboard;          // 是否启用键盘
    bool enable_touch;             // 是否启用触摸
    bool enable_gestures;          // 是否启用手势
    bool enable_gamepad;           // 是否启用游戏手柄
    bool enable_pen;               // 是否启用触控笔
    int32_t max_touch_points;      // 最大触摸点数量
    
    // 性能优化
    bool enable_multithreading;    // 是否启用多线程
    int32_t render_thread_count;   // 渲染线程数量
    bool enable_swap_interval_adaptation; // 是否启用交换间隔自适应
    bool enable_async_texture_upload; // 是否启用异步纹理上传
    bool enable_batch_rendering;   // 是否启用批量渲染
    
    // 调试选项
    int32_t log_level;             // 日志级别 (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG)
    bool enable_tracing;           // 是否启用跟踪
    bool enable_perf_monitoring;   // 是否启用性能监控
    bool enable_debug_logging;     // 是否启用调试日志
    bool debug_mode;               // 调试模式
    bool show_fps_counter;         // 是否显示FPS计数器
    
    // 其他配置
    float background_color[3];     // 背景颜色 RGB
    bool use_hardware_acceleration; // 是否使用硬件加速
    int32_t refresh_rate;          // 刷新率
    bool enable_screensaver;       // 是否启用屏幕保护
    int32_t screensaver_timeout;   // 屏幕保护超时（秒）
} CompositorConfig;

// 获取默认配置
CompositorConfig* compositor_get_default_config(void);

// 设置日志级别
// - level: 日志级别 (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG)
void compositor_set_log_level(int level);

// 合并配置
// - user_config: 用户提供的配置，NULL 则使用默认配置
// 返回: 合并后的配置
CompositorConfig* compositor_merge_config(CompositorConfig* user_config);

// 释放配置
// - config: 要释放的配置
void compositor_free_config(CompositorConfig* config);

// 打印配置信息
// - config: 要打印的配置
void compositor_print_config(const CompositorConfig* config);

#endif // COMPOSITOR_CONFIG_H