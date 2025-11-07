#ifndef COMPOSITOR_PERF_OPT_H
#define COMPOSITOR_PERF_OPT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 性能配置文件
typedef enum {
    PERF_PROFILE_POWER_SAVE = 0,  // 省电模式
    PERF_PROFILE_BALANCED,        // 平衡模式
    PERF_PROFILE_PERFORMANCE,     // 性能模式
    PERF_PROFILE_COUNT
} perf_profile_t;

// 热节流状态
typedef enum {
    THERMAL_STATE_NORMAL = 0,     // 正常状态
    THERMAL_STATE_WARNING,        // 警告状态
    THERMAL_STATE_THROTTLING,     // 节流状态
    THERMAL_STATE_CRITICAL        // 危险状态
} thermal_state_t;

// 性能预算
struct perf_budget {
    float max_cpu_usage;          // 最大CPU使用率
    float max_gpu_usage;          // 最大GPU使用率
    uint64_t max_memory_usage;    // 最大内存使用量(字节)
    uint32_t target_fps;          // 目标帧率
    uint32_t min_fps;             // 最小帧率
    float max_frame_time;         // 最大帧时间(毫秒)
};

// 自适应帧率设置
struct adaptive_fps_settings {
    bool enabled;                 // 是否启用自适应帧率
    uint32_t min_fps;             // 最小帧率
    uint32_t max_fps;             // 最大帧率
    float fps_step_up;            // 帧率增加步长
    float fps_step_down;          // 帧率减少步长
    uint32_t stable_frames;       // 稳定帧数阈值
    float performance_threshold;  // 性能阈值(0-1)
};

// 渲染优化设置
struct render_opt_settings {
    bool adaptive_quality;        // 自适应质量
    uint32_t quality_levels;      // 质量等级数
    uint32_t current_quality;     // 当前质量等级
    bool dirty_regions;           // 脏区域优化
    bool occlusion_culling;       // 遮挡剔除
    bool frustum_culling;         // 视锥剔除
    bool level_of_detail;         // LOD优化
    float lod_distance;           // LOD距离
    bool texture_compression;     // 纹理压缩
    bool texture_streaming;       // 纹理流式加载
    uint32_t texture_cache_size;  // 纹理缓存大小(MB)
};

// 性能优化状态
struct perf_opt_state {
    bool initialized;             // 是否初始化
    perf_profile_t profile;        // 当前性能配置文件
    thermal_state_t thermal_state; // 当前热状态
    struct perf_budget budget;     // 性能预算
    struct adaptive_fps_settings fps_settings; // 自适应帧率设置
    struct render_opt_settings render_settings; // 渲染优化设置
    
    // 内部状态
    uint32_t frame_count;          // 帧计数
    float current_fps;             // 当前帧率
    float avg_frame_time;          // 平均帧时间
    float cpu_usage;               // CPU使用率
    float gpu_usage;               // GPU使用率
    uint64_t memory_usage;         // 内存使用量
    uint32_t performance_issues;   // 性能问题计数
    uint32_t adjustment_count;     // 调整计数
    uint64_t last_adjustment_time; // 上次调整时间
    uint64_t last_stats_update;    // 上次统计更新时间
};

// 性能优化回调
typedef void (*perf_opt_callback_t)(void* user_data);

// 初始化性能优化模块
int perf_opt_init(void);

// 销毁性能优化模块
void perf_opt_destroy(void);

// 更新性能优化模块（每帧调用）
void perf_opt_update(void);

// 设置性能配置文件
void perf_opt_set_profile(perf_profile_t profile);

// 获取性能配置文件
perf_profile_t perf_opt_get_profile(void);

// 设置性能预算
void perf_opt_set_budget(const struct perf_budget* budget);

// 获取性能预算
void perf_opt_get_budget(struct perf_budget* budget);

// 设置自适应帧率设置
void perf_opt_set_adaptive_fps(const struct adaptive_fps_settings* settings);

// 获取自适应帧率设置
void perf_opt_get_adaptive_fps(struct adaptive_fps_settings* settings);

// 启用/禁用自适应帧率
void perf_opt_set_adaptive_fps_enabled(bool enabled);

// 检查自适应帧率是否启用
bool perf_opt_is_adaptive_fps_enabled(void);

// 设置渲染优化设置
void perf_opt_set_render_opt(const struct render_opt_settings* settings);

// 获取渲染优化设置
void perf_opt_get_render_opt(struct render_opt_settings* settings);

// 启用/禁用自适应质量
void perf_opt_set_adaptive_quality_enabled(bool enabled);

// 检查自适应质量是否启用
bool perf_opt_is_adaptive_quality_enabled(void);

// 设置当前质量等级
void perf_opt_set_quality_level(uint32_t level);

// 获取当前质量等级
uint32_t perf_opt_get_quality_level(void);

// 启用/禁用脏区域优化
void perf_opt_set_dirty_regions_enabled(bool enabled);

// 检查脏区域优化是否启用
bool perf_opt_is_dirty_regions_enabled(void);

// 启用/禁用遮挡剔除
void perf_opt_set_occlusion_culling_enabled(bool enabled);

// 检查遮挡剔除是否启用
bool perf_opt_is_occlusion_culling_enabled(void);

// 启用/禁用视锥剔除
void perf_opt_set_frustum_culling_enabled(bool enabled);

// 检查视锥剔除是否启用
bool perf_opt_is_frustum_culling_enabled(void);

// 启用/禁用LOD优化
void perf_opt_set_lod_enabled(bool enabled);

// 检查LOD优化是否启用
bool perf_opt_is_lod_enabled(void);

// 设置LOD距离
void perf_opt_set_lod_distance(float distance);

// 获取LOD距离
float perf_opt_get_lod_distance(void);

// 启用/禁用纹理压缩
void perf_opt_set_texture_compression_enabled(bool enabled);

// 检查纹理压缩是否启用
bool perf_opt_is_texture_compression_enabled(void);

// 启用/禁用纹理流式加载
void perf_opt_set_texture_streaming_enabled(bool enabled);

// 检查纹理流式加载是否启用
bool perf_opt_is_texture_streaming_enabled(void);

// 设置纹理缓存大小
void perf_opt_set_texture_cache_size(uint32_t size_mb);

// 获取纹理缓存大小
uint32_t perf_opt_get_texture_cache_size(void);

// 获取热状态
thermal_state_t perf_opt_get_thermal_state(void);

// 获取当前帧率
float perf_opt_get_current_fps(void);

// 获取平均帧时间
float perf_opt_get_avg_frame_time(void);

// 获取CPU使用率
float perf_opt_get_cpu_usage(void);

// 获取GPU使用率
float perf_opt_get_gpu_usage(void);

// 获取内存使用量
uint64_t perf_opt_get_memory_usage(void);

// 重置性能优化统计
void perf_opt_reset_stats(void);

// 注册性能优化回调
void perf_opt_register_callback(perf_opt_callback_t callback, void* user_data);

// 注销性能优化回调
void perf_opt_unregister_callback(perf_opt_callback_t callback);

// 手动触发性能调整
void perf_opt_trigger_adjustment(void);

// 保存性能优化设置
int perf_opt_save_settings(const char* path);

// 加载性能优化设置
int perf_opt_load_settings(const char* path);

// 打印性能优化状态
void perf_opt_print_status(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_PERF_OPT_H