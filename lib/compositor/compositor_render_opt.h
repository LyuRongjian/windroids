#ifndef COMPOSITOR_RENDER_OPT_H
#define COMPOSITOR_RENDER_OPT_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// 渲染层类型
typedef enum {
    RENDER_LAYER_BACKGROUND = 0,
    RENDER_LAYER_APPLICATION = 1,
    RENDER_LAYER_OVERLAY = 2,
    RENDER_LAYER_UI = 3,
    RENDER_LAYER_CURSOR = 4,
    RENDER_LAYER_COUNT = 5
} render_layer_type_t;

// 脏区域结构
struct dirty_region {
    int x, y;
    int width, height;
};

// 脏区域管理器
struct dirty_region_manager {
    struct dirty_region* regions;
    uint32_t region_count;
    uint32_t max_regions;
    uint32_t total_dirty_area;
    float merge_threshold;  // 合并阈值（屏幕面积的百分比）
};

// 绘制调用结构
struct draw_call {
    void* texture;      // 纹理指针
    void* shader;       // 着色器指针
    uint32_t vertex_count;  // 顶点数量
    uint32_t index_count;   // 索引数量
    int blend_mode;     // 混合模式
    float transform[16]; // 变换矩阵
};

// 渲染管道
struct render_pipeline {
    struct draw_call* draw_calls;
    uint32_t draw_call_count;
    uint32_t max_draw_calls;
    bool batching_enabled;      // 是否启用批处理
    bool state_sorting_enabled; // 是否启用状态排序
    bool culling_enabled;       // 是否启用剔除
};

// 渲染优化统计
struct render_opt_stats {
    uint32_t dirty_region_count;
    uint32_t total_dirty_area;
    uint32_t draw_call_count;
    bool batching_enabled;
    bool state_sorting_enabled;
    bool culling_enabled;
};

// 渲染优化状态
struct render_opt_state {
    bool initialized;
    int screen_width;
    int screen_height;
    struct dirty_region_manager dirty_managers[RENDER_LAYER_COUNT];
    struct render_pipeline pipelines[RENDER_LAYER_COUNT];
    pthread_mutex_t mutex;
};

// 初始化渲染优化模块
int render_opt_init(int screen_width, int screen_height);

// 销毁渲染优化模块
void render_opt_destroy(void);

// 标记区域为脏（优化版本）
void render_opt_mark_dirty(render_layer_type_t layer, int x, int y, int width, int height);

// 清除脏区域（优化版本）
void render_opt_clear_dirty_regions(render_layer_type_t layer);

// 获取脏区域（优化版本）
uint32_t render_opt_get_dirty_regions(render_layer_type_t layer, struct dirty_region* regions, uint32_t max_regions);

// 添加绘制调用
void render_opt_add_draw_call(render_layer_type_t layer, const struct draw_call* draw_call);

// 优化渲染管道
void render_opt_optimize_pipeline(render_layer_type_t layer);

// 获取优化的绘制调用
uint32_t render_opt_get_draw_calls(render_layer_type_t layer, struct draw_call* draw_calls, uint32_t max_draw_calls);

// 清除绘制调用
void render_opt_clear_draw_calls(render_layer_type_t layer);

// 设置批处理启用状态
void render_opt_set_batching_enabled(render_layer_type_t layer, bool enabled);

// 设置状态排序启用状态
void render_opt_set_state_sorting_enabled(render_layer_type_t layer, bool enabled);

// 设置剔除启用状态
void render_opt_set_culling_enabled(render_layer_type_t layer, bool enabled);

// 设置脏区域合并阈值
void render_opt_set_merge_threshold(render_layer_type_t layer, float threshold);

// 获取渲染优化统计
void render_opt_get_stats(render_layer_type_t layer, struct render_opt_stats* stats);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_RENDER_OPT_H