/**
 * @file compositor_vulkan_optimization.h
 * @brief Vulkan 渲染优化头文件
 */

#ifndef COMPOSITOR_VULKAN_OPTIMIZATION_H
#define COMPOSITOR_VULKAN_OPTIMIZATION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明
typedef struct CompositorVulkanState CompositorVulkanState;
typedef struct CompositorConfig CompositorConfig;
typedef struct VulkanRenderOptimization VulkanRenderOptimization;

// 脏区域结构体
typedef struct {
    int x, y;
    int width, height;
} DirtyRegion;

// 渲染批次结构体
typedef struct {
    const char* pipeline;
    const void* vertices;
    int vertex_count;
} RenderBatch;

// 渲染优化配置结构体
typedef struct RenderOptimizationConfig {
    bool enable_dirty_region_optimization;
    bool enable_render_batching;
    bool enable_texture_atlas;
    bool enable_scissor_test;
    bool enable_occlusion_culling;
    int max_batch_size;
    int dirty_region_threshold;
} RenderOptimizationConfig;

// 渲染优化统计结构体
typedef struct RenderOptimizationStats {
    int total_draw_calls;
    int batched_draw_calls;
    int optimized_draw_calls;
    float optimization_ratio;
    int dirty_region_count;
} RenderOptimizationStats;

/**
 * @brief 设置 Vulkan 合成器状态引用
 * @param state Vulkan 合成器状态
 */
void compositor_vulkan_optimization_set_state(CompositorVulkanState* state);

/**
 * @brief 初始化渲染优化配置
 * @param opt Vulkan 渲染优化配置
 * @param config 合成器配置
 * @return 初始化是否成功
 */
bool init_render_optimization(VulkanRenderOptimization* opt, const CompositorConfig* config);

/**
 * @brief 更新渲染优化配置
 * @param opt Vulkan 渲染优化配置
 * @param config 合成器配置
 */
void update_render_optimization(VulkanRenderOptimization* opt, const CompositorConfig* config);

/**
 * @brief 检查是否启用脏区域优化
 * @param opt Vulkan 渲染优化配置
 * @return 是否启用脏区域优化
 */
bool is_dirty_rect_optimization_enabled(const VulkanRenderOptimization* opt);

/**
 * @brief 检查是否启用渲染批处理
 * @param opt Vulkan 渲染优化配置
 * @return 是否启用渲染批处理
 */
bool is_render_batching_enabled(const VulkanRenderOptimization* opt);

/**
 * @brief 检查是否启用实例化渲染
 * @param opt Vulkan 渲染优化配置
 * @return 是否启用实例化渲染
 */
bool is_instanced_rendering_enabled(const VulkanRenderOptimization* opt);

/**
 * @brief 检查是否启用自适应同步
 * @param opt Vulkan 渲染优化配置
 * @return 是否启用自适应同步
 */
bool is_adaptive_sync_enabled(const VulkanRenderOptimization* opt);

/**
 * @brief 检查是否启用动态渲染
 * @param opt Vulkan 渲染优化配置
 * @return 是否启用动态渲染
 */
bool is_dynamic_rendering_enabled(const VulkanRenderOptimization* opt);

/**
 * @brief 获取最大各向异性过滤值
 * @param opt Vulkan 渲染优化配置
 * @return 最大各向异性过滤值
 */
float get_max_anisotropy(const VulkanRenderOptimization* opt);

/**
 * @brief 获取交换间隔
 * @param opt Vulkan 渲染优化配置
 * @return 交换间隔
 */
int get_swap_interval(const VulkanRenderOptimization* opt);

/**
 * @brief 获取自适应同步最小刷新率
 * @param opt Vulkan 渲染优化配置
 * @return 自适应同步最小刷新率
 */
int get_adaptive_sync_min_refresh_rate(const VulkanRenderOptimization* opt);

/**
 * @brief 获取自适应同步最大刷新率
 * @param opt Vulkan 渲染优化配置
 * @return 自适应同步最大刷新率
 */
int get_adaptive_sync_max_refresh_rate(const VulkanRenderOptimization* opt);

/**
 * @brief 初始化脏区域系统
 * @param initial_capacity 初始容量
 * @return 初始化是否成功
 */
bool init_dirty_regions(int initial_capacity);

/**
 * @brief 清理脏区域系统
 */
void cleanup_dirty_regions(void);

/**
 * @brief 标记脏区域
 * @param x X坐标
 * @param y Y坐标
 * @param width 宽度
 * @param height 高度
 */
void mark_dirty_region(int x, int y, int width, int height);

/**
 * @brief 获取脏区域数量
 * @return 脏区域数量
 */
int get_dirty_region_count(void);

/**
 * @brief 获取脏区域列表
 * @return 脏区域列表指针
 */
const DirtyRegion* get_dirty_regions(void);

/**
 * @brief 清除脏区域
 */
void clear_dirty_regions(void);

/**
 * @brief 初始化批处理渲染
 * @param initial_capacity 初始容量
 * @return 初始化是否成功
 */
bool init_batch_rendering(int initial_capacity);

/**
 * @brief 清理批处理渲染
 */
void cleanup_batch_rendering(void);

/**
 * @brief 批处理渲染命令
 * @param pipeline 管线
 * @param vertices 顶点数据
 * @param vertex_count 顶点数量
 */
void batch_render_commands(const char* pipeline, const void* vertices, int vertex_count);

/**
 * @brief 优化渲染批次
 */
void optimize_render_batches(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_VULKAN_OPTIMIZATION_H