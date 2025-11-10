#ifndef COMPOSITOR_RENDER_H
#define COMPOSITOR_RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// 前向声明，避免循环依赖
struct draw_call;
struct render_opt_stats;

#ifdef __cplusplus
extern "C" {
#endif

// 渲染层类型
typedef enum {
    RENDER_LAYER_BACKGROUND,  // 背景层
    RENDER_LAYER_WINDOW,      // 窗口层
    RENDER_LAYER_UI,          // UI层
    RENDER_LAYER_OVERLAY,     // 覆盖层
    RENDER_LAYER_COUNT        // 层数量
} render_layer_type_t;

// 脏区域结构
struct dirty_region {
    int x, y;                 // 区域位置
    int width, height;        // 区域大小
    struct dirty_region* next; // 下一个脏区域
};

// 渲染目标
struct render_target {
    uint32_t id;              // 目标ID
    uint32_t texture;         // 纹理ID
    int width, height;        // 目标大小
    bool dirty;               // 是否需要更新
    struct dirty_region* dirty_regions; // 脏区域列表
};

// 渲染层
struct render_layer {
    render_layer_type_t type; // 层类型
    struct render_target* targets; // 渲染目标列表
    uint32_t target_count;    // 目标数量
    bool visible;             // 是否可见
    float opacity;            // 不透明度
};

// 渲染统计
struct render_stats {
    uint32_t frame_count;     // 帧计数
    float fps;                // 帧率
    uint32_t draw_calls;      // 绘制调用次数
    uint32_t triangles;       // 三角形数量
    uint32_t texture_switches; // 纹理切换次数
    float cpu_time;           // CPU时间
    float gpu_time;           // GPU时间
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

// 渲染优化统计
struct render_opt_stats {
    uint32_t dirty_region_count;
    uint32_t total_dirty_area;
    uint32_t draw_call_count;
    bool batching_enabled;
    bool state_sorting_enabled;
    bool culling_enabled;
};

// 渲染器状态
struct renderer {
    struct render_layer layers[RENDER_LAYER_COUNT]; // 渲染层
    uint32_t next_target_id;  // 下一个目标ID
    struct render_stats stats; // 渲染统计
    bool vsync_enabled;       // 是否启用垂直同步
    uint32_t max_fps;         // 最大帧率
    uint32_t target_fps;      // 目标帧率
    uint64_t last_frame_time; // 上一帧时间
    bool dirty_regions_enabled; // 是否启用脏区域优化
    bool multithreading_enabled; // 是否启用多线程渲染
};

// 初始化渲染器
int renderer_init(int screen_width, int screen_height);

// 销毁渲染器
void renderer_destroy(void);

// 创建渲染目标
struct render_target* renderer_create_target(int width, int height);

// 销毁渲染目标
void renderer_destroy_target(struct render_target* target);

// 添加渲染目标到层
int renderer_add_target_to_layer(struct render_target* target, render_layer_type_t layer);

// 从层中移除渲染目标
int renderer_remove_target_from_layer(struct render_target* target, render_layer_type_t layer);

// 标记区域为脏
void renderer_mark_dirty(int x, int y, int width, int height);

// 标记目标为脏
void renderer_mark_target_dirty(struct render_target* target);

// 清除脏区域
void renderer_clear_dirty_regions(void);

// 设置层可见性
void renderer_set_layer_visibility(render_layer_type_t layer, bool visible);

// 设置层不透明度
void renderer_set_layer_opacity(render_layer_type_t layer, float opacity);

// 设置垂直同步
void renderer_set_vsync(bool enabled);

// 设置最大帧率
void renderer_set_max_fps(uint32_t fps);

// 设置目标帧率
void renderer_set_target_fps(uint32_t fps);

// 启用/禁用脏区域优化
void renderer_set_dirty_regions_enabled(bool enabled);

// 启用/禁用多线程渲染
void renderer_set_multithreading_enabled(bool enabled);

// 开始渲染帧
int renderer_begin_frame(void);

// 结束渲染帧
int renderer_end_frame(void);

// 渲染层
int renderer_render_layer(render_layer_type_t layer);

// 渲染目标
int renderer_render_target(struct render_target* target);

// 获取渲染统计
void renderer_get_stats(struct render_stats* stats);

// 重置渲染统计
void renderer_reset_stats(void);

// 更新渲染器（每帧调用）
void renderer_update(void);

// 游戏模式相关函数
void renderer_set_frame_pacing_enabled(bool enabled);
void renderer_set_latency_optimization_enabled(bool enabled);
void renderer_set_max_latency(uint32_t latency_ms);
void renderer_set_triple_buffering_enabled(bool enabled);

// 渲染优化相关函数
void renderer_set_batching_enabled(render_layer_type_t layer, bool enabled);
void renderer_set_state_sorting_enabled(render_layer_type_t layer, bool enabled);
void renderer_set_culling_enabled(render_layer_type_t layer, bool enabled);
void renderer_set_merge_threshold(render_layer_type_t layer, float threshold);
void renderer_add_draw_call(render_layer_type_t layer, const struct draw_call* draw_call);
void renderer_get_opt_stats(render_layer_type_t layer, struct render_opt_stats* stats);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_RENDER_H