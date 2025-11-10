#include "compositor_render_opt.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "RenderOpt"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// 全局渲染优化状态
static struct render_opt_state g_render_opt = {0};

// 内部函数声明
static void dirty_region_manager_merge_regions(struct dirty_region_manager* manager);
static bool dirty_region_manager_intersect(const struct dirty_region* a, const struct dirty_region* b);
static void dirty_region_manager_union(struct dirty_region* a, const struct dirty_region* b);
static void dirty_region_manager_optimize_regions(struct dirty_region_manager* manager);
static void render_pipeline_optimize_batch(struct render_pipeline* pipeline);
static void render_pipeline_sort_draw_calls(struct render_pipeline* pipeline);
static bool draw_call_compare(const struct draw_call* a, const struct draw_call* b);

// 初始化渲染优化模块
int render_opt_init(int screen_width, int screen_height) {
    if (g_render_opt.initialized) {
        LOGE("Render optimization module already initialized");
        return -1;
    }
    
    memset(&g_render_opt, 0, sizeof(g_render_opt));
    g_render_opt.screen_width = screen_width;
    g_render_opt.screen_height = screen_height;
    
    // 初始化脏区域管理器
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        g_render_opt.dirty_managers[i].region_count = 0;
        g_render_opt.dirty_managers[i].max_regions = 32;
        g_render_opt.dirty_managers[i].regions = (struct dirty_region*)calloc(
            g_render_opt.dirty_managers[i].max_regions, sizeof(struct dirty_region));
        
        if (!g_render_opt.dirty_managers[i].regions) {
            LOGE("Failed to allocate memory for dirty regions");
            render_opt_destroy();
            return -1;
        }
        
        g_render_opt.dirty_managers[i].total_dirty_area = 0;
        g_render_opt.dirty_managers[i].merge_threshold = 0.5f; // 50%的屏幕面积阈值
    }
    
    // 初始化渲染管道
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        g_render_opt.pipelines[i].draw_calls = NULL;
        g_render_opt.pipelines[i].draw_call_count = 0;
        g_render_opt.pipelines[i].max_draw_calls = 256;
        g_render_opt.pipelines[i].draw_calls = (struct draw_call*)calloc(
            g_render_opt.pipelines[i].max_draw_calls, sizeof(struct draw_call));
        
        if (!g_render_opt.pipelines[i].draw_calls) {
            LOGE("Failed to allocate memory for draw calls");
            render_opt_destroy();
            return -1;
        }
        
        g_render_opt.pipelines[i].batching_enabled = true;
        g_render_opt.pipelines[i].state_sorting_enabled = true;
        g_render_opt.pipelines[i].culling_enabled = true;
    }
    
    g_render_opt.initialized = true;
    LOGI("Render optimization module initialized");
    return 0;
}

// 销毁渲染优化模块
void render_opt_destroy(void) {
    if (!g_render_opt.initialized) {
        return;
    }
    
    // 释放脏区域管理器
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        free(g_render_opt.dirty_managers[i].regions);
        g_render_opt.dirty_managers[i].regions = NULL;
    }
    
    // 释放渲染管道
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        free(g_render_opt.pipelines[i].draw_calls);
        g_render_opt.pipelines[i].draw_calls = NULL;
    }
    
    g_render_opt.initialized = false;
    LOGI("Render optimization module destroyed");
}

// 标记区域为脏（优化版本）
void render_opt_mark_dirty(render_layer_type_t layer, int x, int y, int width, int height) {
    if (!g_render_opt.initialized || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    // 裁剪到屏幕范围
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > g_render_opt.screen_width) {
        width = g_render_opt.screen_width - x;
    }
    if (y + height > g_render_opt.screen_height) {
        height = g_render_opt.screen_height - y;
    }
    
    if (width <= 0 || height <= 0) {
        return;
    }
    
    struct dirty_region_manager* manager = &g_render_opt.dirty_managers[layer];
    
    // 检查是否需要扩展区域数组
    if (manager->region_count >= manager->max_regions) {
        uint32_t new_max = manager->max_regions * 2;
        struct dirty_region* new_regions = (struct dirty_region*)realloc(
            manager->regions, new_max * sizeof(struct dirty_region));
        
        if (!new_regions) {
            LOGE("Failed to expand dirty regions array");
            return;
        }
        
        manager->regions = new_regions;
        manager->max_regions = new_max;
    }
    
    // 添加新的脏区域
    struct dirty_region* region = &manager->regions[manager->region_count++];
    region->x = x;
    region->y = y;
    region->width = width;
    region->height = height;
    
    // 更新总脏区域面积
    manager->total_dirty_area += width * height;
    
    // 如果脏区域面积超过阈值，则合并所有区域
    float screen_area = g_render_opt.screen_width * g_render_opt.screen_height;
    if (manager->total_dirty_area > screen_area * manager->merge_threshold) {
        dirty_region_manager_merge_regions(manager);
    }
}

// 清除脏区域（优化版本）
void render_opt_clear_dirty_regions(render_layer_type_t layer) {
    if (!g_render_opt.initialized || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    struct dirty_region_manager* manager = &g_render_opt.dirty_managers[layer];
    manager->region_count = 0;
    manager->total_dirty_area = 0;
}

// 获取脏区域（优化版本）
uint32_t render_opt_get_dirty_regions(render_layer_type_t layer, struct dirty_region* regions, uint32_t max_regions) {
    if (!g_render_opt.initialized || !regions || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return 0;
    }
    
    struct dirty_region_manager* manager = &g_render_opt.dirty_managers[layer];
    
    // 优化脏区域
    dirty_region_manager_optimize_regions(manager);
    
    // 复制脏区域
    uint32_t count = manager->region_count < max_regions ? manager->region_count : max_regions;
    memcpy(regions, manager->regions, count * sizeof(struct dirty_region));
    
    return count;
}

// 添加绘制调用
void render_opt_add_draw_call(render_layer_type_t layer, const struct draw_call* draw_call) {
    if (!g_render_opt.initialized || !draw_call || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    struct render_pipeline* pipeline = &g_render_opt.pipelines[layer];
    
    // 检查是否需要扩展绘制调用数组
    if (pipeline->draw_call_count >= pipeline->max_draw_calls) {
        uint32_t new_max = pipeline->max_draw_calls * 2;
        struct draw_call* new_draw_calls = (struct draw_call*)realloc(
            pipeline->draw_calls, new_max * sizeof(struct draw_call));
        
        if (!new_draw_calls) {
            LOGE("Failed to expand draw calls array");
            return;
        }
        
        pipeline->draw_calls = new_draw_calls;
        pipeline->max_draw_calls = new_max;
    }
    
    // 添加新的绘制调用
    pipeline->draw_calls[pipeline->draw_call_count++] = *draw_call;
}

// 优化渲染管道
void render_opt_optimize_pipeline(render_layer_type_t layer) {
    if (!g_render_opt.initialized || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    struct render_pipeline* pipeline = &g_render_opt.pipelines[layer];
    
    // 视锥剔除
    if (pipeline->culling_enabled) {
        // 实际实现中应该执行视锥剔除
    }
    
    // 状态排序
    if (pipeline->state_sorting_enabled) {
        render_pipeline_sort_draw_calls(pipeline);
    }
    
    // 批处理优化
    if (pipeline->batching_enabled) {
        render_pipeline_optimize_batch(pipeline);
    }
}

// 获取优化的绘制调用
uint32_t render_opt_get_draw_calls(render_layer_type_t layer, struct draw_call* draw_calls, uint32_t max_draw_calls) {
    if (!g_render_opt.initialized || !draw_calls || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return 0;
    }
    
    struct render_pipeline* pipeline = &g_render_opt.pipelines[layer];
    
    // 复制绘制调用
    uint32_t count = pipeline->draw_call_count < max_draw_calls ? pipeline->draw_call_count : max_draw_calls;
    memcpy(draw_calls, pipeline->draw_calls, count * sizeof(struct draw_call));
    
    return count;
}

// 清除绘制调用
void render_opt_clear_draw_calls(render_layer_type_t layer) {
    if (!g_render_opt.initialized || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    struct render_pipeline* pipeline = &g_render_opt.pipelines[layer];
    pipeline->draw_call_count = 0;
}

// 设置批处理启用状态
void render_opt_set_batching_enabled(render_layer_type_t layer, bool enabled) {
    if (!g_render_opt.initialized || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    g_render_opt.pipelines[layer].batching_enabled = enabled;
}

// 设置状态排序启用状态
void render_opt_set_state_sorting_enabled(render_layer_type_t layer, bool enabled) {
    if (!g_render_opt.initialized || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    g_render_opt.pipelines[layer].state_sorting_enabled = enabled;
}

// 设置剔除启用状态
void render_opt_set_culling_enabled(render_layer_type_t layer, bool enabled) {
    if (!g_render_opt.initialized || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    g_render_opt.pipelines[layer].culling_enabled = enabled;
}

// 设置脏区域合并阈值
void render_opt_set_merge_threshold(render_layer_type_t layer, float threshold) {
    if (!g_render_opt.initialized || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 1.0f) threshold = 1.0f;
    
    g_render_opt.dirty_managers[layer].merge_threshold = threshold;
}

// 获取渲染优化统计
void render_opt_get_stats(render_layer_type_t layer, struct render_opt_stats* stats) {
    if (!g_render_opt.initialized || !stats || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    struct dirty_region_manager* manager = &g_render_opt.dirty_managers[layer];
    struct render_pipeline* pipeline = &g_render_opt.pipelines[layer];
    
    stats->dirty_region_count = manager->region_count;
    stats->total_dirty_area = manager->total_dirty_area;
    stats->draw_call_count = pipeline->draw_call_count;
    stats->batching_enabled = pipeline->batching_enabled;
    stats->state_sorting_enabled = pipeline->state_sorting_enabled;
    stats->culling_enabled = pipeline->culling_enabled;
}

// 内部函数：合并脏区域
static void dirty_region_manager_merge_regions(struct dirty_region_manager* manager) {
    if (manager->region_count <= 1) {
        return;
    }
    
    // 简化实现：合并所有区域为一个大的区域
    int min_x = manager->regions[0].x;
    int min_y = manager->regions[0].y;
    int max_x = manager->regions[0].x + manager->regions[0].width;
    int max_y = manager->regions[0].y + manager->regions[0].height;
    
    for (uint32_t i = 1; i < manager->region_count; i++) {
        struct dirty_region* region = &manager->regions[i];
        
        if (region->x < min_x) min_x = region->x;
        if (region->y < min_y) min_y = region->y;
        if (region->x + region->width > max_x) max_x = region->x + region->width;
        if (region->y + region->height > max_y) max_y = region->y + region->height;
    }
    
    // 设置合并后的区域
    manager->regions[0].x = min_x;
    manager->regions[0].y = min_y;
    manager->regions[0].width = max_x - min_x;
    manager->regions[0].height = max_y - min_y;
    manager->region_count = 1;
    manager->total_dirty_area = manager->regions[0].width * manager->regions[0].height;
}

// 内部函数：检查两个区域是否相交
static bool dirty_region_manager_intersect(const struct dirty_region* a, const struct dirty_region* b) {
    return !(a->x + a->width <= b->x || 
             b->x + b->width <= a->x || 
             a->y + a->height <= b->y || 
             b->y + b->height <= a->y);
}

// 内部函数：合并两个区域
static void dirty_region_manager_union(struct dirty_region* a, const struct dirty_region* b) {
    int min_x = a->x < b->x ? a->x : b->x;
    int min_y = a->y < b->y ? a->y : b->y;
    int max_x = (a->x + a->width) > (b->x + b->width) ? (a->x + a->width) : (b->x + b->width);
    int max_y = (a->y + a->height) > (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);
    
    a->x = min_x;
    a->y = min_y;
    a->width = max_x - min_x;
    a->height = max_y - min_y;
}

// 内部函数：优化脏区域
static void dirty_region_manager_optimize_regions(struct dirty_region_manager* manager) {
    if (manager->region_count <= 1) {
        return;
    }
    
    // 移除重叠区域
    bool changed;
    do {
        changed = false;
        
        for (uint32_t i = 0; i < manager->region_count && !changed; i++) {
            for (uint32_t j = i + 1; j < manager->region_count; j++) {
                if (dirty_region_manager_intersect(&manager->regions[i], &manager->regions[j])) {
                    // 合并区域
                    dirty_region_manager_union(&manager->regions[i], &manager->regions[j]);
                    
                    // 移除区域j
                    for (uint32_t k = j; k < manager->region_count - 1; k++) {
                        manager->regions[k] = manager->regions[k + 1];
                    }
                    manager->region_count--;
                    
                    changed = true;
                    break;
                }
            }
        }
    } while (changed);
}

// 内部函数：优化批处理
static void render_pipeline_optimize_batch(struct render_pipeline* pipeline) {
    if (pipeline->draw_call_count <= 1) {
        return;
    }
    
    // 简化实现：合并具有相同状态的绘制调用
    for (uint32_t i = 0; i < pipeline->draw_call_count - 1; i++) {
        struct draw_call* current = &pipeline->draw_calls[i];
        struct draw_call* next = &pipeline->draw_calls[i + 1];
        
        // 检查是否可以批处理
        if (current->texture == next->texture &&
            current->shader == next->shader &&
            current->blend_mode == next->blend_mode) {
            
            // 合并绘制调用
            // 实际实现中应该合并顶点数据
            current->vertex_count += next->vertex_count;
            
            // 移除下一个绘制调用
            for (uint32_t j = i + 1; j < pipeline->draw_call_count - 1; j++) {
                pipeline->draw_calls[j] = pipeline->draw_calls[j + 1];
            }
            pipeline->draw_call_count--;
            
            // 重新检查当前绘制调用
            i--;
        }
    }
}

// 内部函数：排序绘制调用
static void render_pipeline_sort_draw_calls(struct render_pipeline* pipeline) {
    if (pipeline->draw_call_count <= 1) {
        return;
    }
    
    // 简化实现：使用冒泡排序按纹理排序
    for (uint32_t i = 0; i < pipeline->draw_call_count - 1; i++) {
        for (uint32_t j = 0; j < pipeline->draw_call_count - i - 1; j++) {
            if (draw_call_compare(&pipeline->draw_calls[j], &pipeline->draw_calls[j + 1])) {
                // 交换绘制调用
                struct draw_call temp = pipeline->draw_calls[j];
                pipeline->draw_calls[j] = pipeline->draw_calls[j + 1];
                pipeline->draw_calls[j + 1] = temp;
            }
        }
    }
}

// 内部函数：比较绘制调用
static bool draw_call_compare(const struct draw_call* a, const struct draw_call* b) {
    // 按纹理、着色器、混合模式的优先级排序
    if (a->texture != b->texture) {
        return a->texture > b->texture;
    }
    if (a->shader != b->shader) {
        return a->shader > b->shader;
    }
    return a->blend_mode > b->blend_mode;
}