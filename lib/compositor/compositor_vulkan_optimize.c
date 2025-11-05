/*
 * WinDroids Compositor
 * Vulkan Rendering Optimization Implementation
 */

#include "compositor_vulkan_optimize.h"
#include "compositor_utils.h"
#include "compositor_perf.h"
#include <string.h>
#include <stdlib.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 设置合成器状态引用
void compositor_vulkan_optimize_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 初始化渲染优化系统
int init_render_optimization(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 设置默认优化配置
    g_compositor_state->vulkan.render_optimization.dirty_regions_enabled = true;
    g_compositor_state->vulkan.render_optimization.batch_rendering_enabled = true;
    g_compositor_state->vulkan.render_optimization.max_batch_size = 32;
    g_compositor_state->vulkan.render_optimization.texture_atlas_enabled = true;
    g_compositor_state->vulkan.render_optimization.use_scissor_test = true;
    
    // 初始化脏区域系统
    int ret = init_dirty_regions();
    if (ret != COMPOSITOR_OK) {
        return ret;
    }
    
    // 初始化批处理系统
    ret = init_batch_rendering();
    if (ret != COMPOSITOR_OK) {
        cleanup_dirty_regions();
        return ret;
    }
    
    // 初始化统计信息
    memset(&g_compositor_state->vulkan.optimization_stats, 0, sizeof(RenderOptimizationStats));
    g_compositor_state->vulkan.optimization_stats.start_time = get_current_time_ms();
    
    log_message(COMPOSITOR_LOG_INFO, "Render optimization system initialized");
    return COMPOSITOR_OK;
}

// 清理渲染优化系统
void cleanup_render_optimization(void) {
    if (!g_compositor_state) {
        return;
    }
    
    // 清理批处理系统
    cleanup_batch_rendering();
    
    // 清理脏区域系统
    cleanup_dirty_regions();
    
    log_message(COMPOSITOR_LOG_INFO, "Render optimization system cleaned up");
}

// 设置渲染优化配置
void set_render_optimization_config(const RenderOptimizationConfig* config) {
    if (!g_compositor_state || !config) {
        return;
    }
    
    g_compositor_state->vulkan.render_optimization = *config;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Render optimization config updated");
}

// 获取渲染优化配置
void get_render_optimization_config(RenderOptimizationConfig* out_config) {
    if (!g_compositor_state || !out_config) {
        return;
    }
    
    *out_config = g_compositor_state->vulkan.render_optimization;
}

// 启用脏区域优化
void enable_dirty_region_optimization(bool enable) {
    if (g_compositor_state) {
        g_compositor_state->vulkan.render_optimization.dirty_regions_enabled = enable;
        log_message(COMPOSITOR_LOG_INFO, "Dirty region optimization %s", 
                   enable ? "enabled" : "disabled");
    }
}

// 启用批处理渲染
void enable_batch_rendering(bool enable) {
    if (g_compositor_state) {
        g_compositor_state->vulkan.render_optimization.batch_rendering_enabled = enable;
        log_message(COMPOSITOR_LOG_INFO, "Batch rendering %s", 
                   enable ? "enabled" : "disabled");
    }
}

// 启用纹理图集
void enable_texture_atlas(bool enable) {
    if (g_compositor_state) {
        g_compositor_state->vulkan.render_optimization.texture_atlas_enabled = enable;
        log_message(COMPOSITOR_LOG_INFO, "Texture atlas %s", 
                   enable ? "enabled" : "disabled");
    }
}

// 启用裁剪测试
void enable_scissor_test(bool enable) {
    if (g_compositor_state) {
        g_compositor_state->vulkan.render_optimization.use_scissor_test = enable;
        log_message(COMPOSITOR_LOG_INFO, "Scissor test %s", 
                   enable ? "enabled" : "disabled");
    }
}

// 设置最大批处理大小
void set_max_batch_size(int max_size) {
    if (g_compositor_state && max_size > 0) {
        g_compositor_state->vulkan.render_optimization.max_batch_size = max_size;
        log_message(COMPOSITOR_LOG_INFO, "Max batch size set to: %d", max_size);
    }
}

// 初始化脏区域系统
int init_dirty_regions(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 初始化脏区域数组
    g_compositor_state->vulkan.dirty_regions_capacity = 10;
    g_compositor_state->vulkan.dirty_regions_count = 0;
    g_compositor_state->vulkan.dirty_regions = 
        (DirtyRegion*)safe_malloc(g_compositor_state->vulkan.dirty_regions_capacity * 
                                sizeof(DirtyRegion));
    if (!g_compositor_state->vulkan.dirty_regions) {
        return COMPOSITOR_ERROR_OUT_OF_MEMORY;
    }
    
    g_compositor_state->vulkan.dirty_regions_dirty = false;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Dirty regions system initialized");
    return COMPOSITOR_OK;
}

// 清理脏区域系统
void cleanup_dirty_regions(void) {
    if (!g_compositor_state) {
        return;
    }
    
    if (g_compositor_state->vulkan.dirty_regions) {
        free(g_compositor_state->vulkan.dirty_regions);
        g_compositor_state->vulkan.dirty_regions = NULL;
    }
    
    g_compositor_state->vulkan.dirty_regions_count = 0;
    g_compositor_state->vulkan.dirty_regions_capacity = 0;
    g_compositor_state->vulkan.dirty_regions_dirty = false;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Dirty regions system cleaned up");
}

// 标记脏区域
void mark_dirty_region(int x, int y, int width, int height) {
    if (!g_compositor_state) {
        return;
    }
    
    // 检查区域是否有效
    if (width <= 0 || height <= 0) {
        return;
    }
    
    // 确保脏区域数组有足够的空间
    if (g_compositor_state->vulkan.dirty_regions_count >= 
        g_compositor_state->vulkan.dirty_regions_capacity) {
        int new_capacity = g_compositor_state->vulkan.dirty_regions_capacity * 2;
        DirtyRegion* new_regions = 
            (DirtyRegion*)safe_realloc(g_compositor_state->vulkan.dirty_regions, 
                                     new_capacity * sizeof(DirtyRegion));
        if (!new_regions) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to expand dirty regions array");
            return;
        }
        
        g_compositor_state->vulkan.dirty_regions = new_regions;
        g_compositor_state->vulkan.dirty_regions_capacity = new_capacity;
    }
    
    // 添加新的脏区域
    DirtyRegion* region = &g_compositor_state->vulkan.dirty_regions[
        g_compositor_state->vulkan.dirty_regions_count];
    region->x = x;
    region->y = y;
    region->width = width;
    region->height = height;
    
    g_compositor_state->vulkan.dirty_regions_count++;
    g_compositor_state->vulkan.dirty_regions_dirty = true;
    
    // 更新统计信息
    g_compositor_state->vulkan.optimization_stats.dirty_regions_marked++;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Marked dirty region: %d,%d %dx%d", x, y, width, height);
}

// 清除所有脏区域
void clear_dirty_regions(void) {
    if (g_compositor_state) {
        g_compositor_state->vulkan.dirty_regions_count = 0;
        g_compositor_state->vulkan.dirty_regions_dirty = false;
        
        // 更新统计信息
        g_compositor_state->vulkan.optimization_stats.dirty_regions_cleared++;
        
        log_message(COMPOSITOR_LOG_DEBUG, "Cleared all dirty regions");
    }
}

// 获取所有脏区域
DirtyRegion* get_dirty_regions(int* out_count) {
    if (!g_compositor_state) {
        if (out_count) {
            *out_count = 0;
        }
        return NULL;
    }
    
    if (out_count) {
        *out_count = g_compositor_state->vulkan.dirty_regions_count;
    }
    
    return g_compositor_state->vulkan.dirty_regions;
}

// 合并重叠的脏区域
void merge_overlapping_dirty_regions(void) {
    if (!g_compositor_state || g_compositor_state->vulkan.dirty_regions_count <= 1) {
        return;
    }
    
    compositor_perf_start_measurement(COMPOSITOR_PERF_DIRTY_REGION_MERGE);
    
    log_message(COMPOSITOR_LOG_DEBUG, "Merging overlapping dirty regions");
    
    // 这里实现脏区域合并算法
    // 简化版：使用贪婪算法合并重叠区域
    int merged_count = 0;
    
    for (int i = 0; i < g_compositor_state->vulkan.dirty_regions_count; i++) {
        bool merged = false;
        
        for (int j = 0; j < merged_count; j++) {
            if (regions_overlap(&g_compositor_state->vulkan.dirty_regions[i], 
                               &g_compositor_state->vulkan.dirty_regions[j])) {
                // 合并两个区域
                merge_two_regions(&g_compositor_state->vulkan.dirty_regions[j], 
                                 &g_compositor_state->vulkan.dirty_regions[i]);
                merged = true;
                break;
            }
        }
        
        if (!merged) {
            // 复制当前区域到合并列表
            g_compositor_state->vulkan.dirty_regions[merged_count] = 
                g_compositor_state->vulkan.dirty_regions[i];
            merged_count++;
        }
    }
    
    int original_count = g_compositor_state->vulkan.dirty_regions_count;
    g_compositor_state->vulkan.dirty_regions_count = merged_count;
    
    // 更新统计信息
    g_compositor_state->vulkan.optimization_stats.dirty_regions_merged += 
        original_count - merged_count;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Merged %d dirty regions into %d", 
               original_count, merged_count);
    
    compositor_perf_end_measurement(COMPOSITOR_PERF_DIRTY_REGION_MERGE);
}

// 检查两个区域是否重叠
bool regions_overlap(const DirtyRegion* a, const DirtyRegion* b) {
    if (!a || !b) {
        return false;
    }
    
    bool overlap_x = !(a->x + a->width <= b->x || b->x + b->width <= a->x);
    bool overlap_y = !(a->y + a->height <= b->y || b->y + b->height <= a->y);
    
    return overlap_x && overlap_y;
}

// 合并两个区域
void merge_two_regions(DirtyRegion* dest, const DirtyRegion* src) {
    if (!dest || !src) {
        return;
    }
    
    int min_x = dest->x < src->x ? dest->x : src->x;
    int min_y = dest->y < src->y ? dest->y : src->y;
    int max_x = (dest->x + dest->width) > (src->x + src->width) ? 
                (dest->x + dest->width) : (src->x + src->width);
    int max_y = (dest->y + dest->height) > (src->y + src->height) ? 
                (dest->y + dest->height) : (src->y + src->height);
    
    dest->x = min_x;
    dest->y = min_y;
    dest->width = max_x - min_x;
    dest->height = max_y - min_y;
}

// 初始化批处理系统
int init_batch_rendering(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 初始化批处理状态
    g_compositor_state->vulkan.batch_state.current_batch_index = 0;
    g_compositor_state->vulkan.batch_state.batch_count = 0;
    g_compositor_state->vulkan.batch_state.batch_capacity = 10;
    
    // 分配批处理数组
    g_compositor_state->vulkan.batch_state.batches = 
        (RenderBatch*)safe_malloc(g_compositor_state->vulkan.batch_state.batch_capacity * 
                                sizeof(RenderBatch));
    if (!g_compositor_state->vulkan.batch_state.batches) {
        return COMPOSITOR_ERROR_OUT_OF_MEMORY;
    }
    
    // 初始化每个批次
    for (int i = 0; i < g_compositor_state->vulkan.batch_state.batch_capacity; i++) {
        g_compositor_state->vulkan.batch_state.batches[i].command_count = 0;
        g_compositor_state->vulkan.batch_state.batches[i].command_capacity = 
            g_compositor_state->vulkan.render_optimization.max_batch_size;
        g_compositor_state->vulkan.batch_state.batches[i].commands = 
            (DrawCommand*)safe_malloc(g_compositor_state->vulkan.batch_state.batches[i].command_capacity * 
                                    sizeof(DrawCommand));
        if (!g_compositor_state->vulkan.batch_state.batches[i].commands) {
            // 清理已分配的内存
            for (int j = 0; j < i; j++) {
                free(g_compositor_state->vulkan.batch_state.batches[j].commands);
            }
            free(g_compositor_state->vulkan.batch_state.batches);
            g_compositor_state->vulkan.batch_state.batches = NULL;
            return COMPOSITOR_ERROR_OUT_OF_MEMORY;
        }
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Batch rendering system initialized");
    return COMPOSITOR_OK;
}

// 清理批处理系统
void cleanup_batch_rendering(void) {
    if (!g_compositor_state) {
        return;
    }
    
    if (g_compositor_state->vulkan.batch_state.batches) {
        // 释放每个批次的命令数组
        for (int i = 0; i < g_compositor_state->vulkan.batch_state.batch_capacity; i++) {
            if (g_compositor_state->vulkan.batch_state.batches[i].commands) {
                free(g_compositor_state->vulkan.batch_state.batches[i].commands);
                g_compositor_state->vulkan.batch_state.batches[i].commands = NULL;
            }
        }
        
        // 释放批次数组
        free(g_compositor_state->vulkan.batch_state.batches);
        g_compositor_state->vulkan.batch_state.batches = NULL;
    }
    
    // 重置批处理状态
    g_compositor_state->vulkan.batch_state.current_batch_index = 0;
    g_compositor_state->vulkan.batch_state.batch_count = 0;
    g_compositor_state->vulkan.batch_state.batch_capacity = 0;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Batch rendering system cleaned up");
}

// 开始新的渲染批次
int start_new_batch(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 如果当前批次已满或还没有批次，创建新的批次
    if (g_compositor_state->vulkan.batch_state.batch_count >= 
        g_compositor_state->vulkan.batch_state.batch_capacity) {
        // 扩展批次数组
        int new_capacity = g_compositor_state->vulkan.batch_state.batch_capacity * 2;
        RenderBatch* new_batches = 
            (RenderBatch*)safe_realloc(g_compositor_state->vulkan.batch_state.batches, 
                                     new_capacity * sizeof(RenderBatch));
        if (!new_batches) {
            return COMPOSITOR_ERROR_OUT_OF_MEMORY;
        }
        
        g_compositor_state->vulkan.batch_state.batches = new_batches;
        
        // 初始化新增的批次
        for (int i = g_compositor_state->vulkan.batch_state.batch_capacity; 
             i < new_capacity; i++) {
            new_batches[i].command_count = 0;
            new_batches[i].command_capacity = 
                g_compositor_state->vulkan.render_optimization.max_batch_size;
            new_batches[i].commands = 
                (DrawCommand*)safe_malloc(new_batches[i].command_capacity * 
                                        sizeof(DrawCommand));
            if (!new_batches[i].commands) {
                return COMPOSITOR_ERROR_OUT_OF_MEMORY;
            }
        }
        
        g_compositor_state->vulkan.batch_state.batch_capacity = new_capacity;
    }
    
    // 设置当前批次索引
    g_compositor_state->vulkan.batch_state.current_batch_index = 
        g_compositor_state->vulkan.batch_state.batch_count;
    
    // 重置当前批次的命令计数
    g_compositor_state->vulkan.batch_state.batches[
        g_compositor_state->vulkan.batch_state.current_batch_index].command_count = 0;
    
    // 增加批次计数
    g_compositor_state->vulkan.batch_state.batch_count++;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Started new batch, current index: %d", 
               g_compositor_state->vulkan.batch_state.current_batch_index);
    
    return COMPOSITOR_OK;
}

// 结束当前渲染批次
int end_current_batch(VkCommandBuffer command_buffer) {
    if (!g_compositor_state || 
        g_compositor_state->vulkan.batch_state.current_batch_index < 0 ||
        g_compositor_state->vulkan.batch_state.current_batch_index >= 
        g_compositor_state->vulkan.batch_state.batch_count) {
        return COMPOSITOR_ERROR_INVALID_STATE;
    }
    
    // 执行当前批次的所有命令
    RenderBatch* current_batch = &g_compositor_state->vulkan.batch_state.batches[
        g_compositor_state->vulkan.batch_state.current_batch_index];
    
    if (current_batch->command_count > 0) {
        execute_batch_commands(command_buffer, current_batch);
        
        // 更新统计信息
        g_compositor_state->vulkan.optimization_stats.batches_executed++;
        g_compositor_state->vulkan.optimization_stats.commands_executed += 
            current_batch->command_count;
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Ended batch, executed %d commands", 
               current_batch->command_count);
    
    return COMPOSITOR_OK;
}

// 添加绘制命令到当前批次
int add_draw_command_to_batch(DrawCommand* command) {
    if (!g_compositor_state || !command) {
        return COMPOSITOR_ERROR_INVALID_PARAMETER;
    }
    
    if (g_compositor_state->vulkan.batch_state.current_batch_index < 0 ||
        g_compositor_state->vulkan.batch_state.current_batch_index >= 
        g_compositor_state->vulkan.batch_state.batch_count) {
        // 如果没有当前批次，开始一个新批次
        int ret = start_new_batch();
        if (ret != COMPOSITOR_OK) {
            return ret;
        }
    }
    
    RenderBatch* current_batch = &g_compositor_state->vulkan.batch_state.batches[
        g_compositor_state->vulkan.batch_state.current_batch_index];
    
    // 检查当前批次是否已满
    if (current_batch->command_count >= current_batch->command_capacity) {
        // 如果启用了批处理渲染，创建新批次
        if (g_compositor_state->vulkan.render_optimization.batch_rendering_enabled) {
            int ret = start_new_batch();
            if (ret != COMPOSITOR_OK) {
                return ret;
            }
            current_batch = &g_compositor_state->vulkan.batch_state.batches[
                g_compositor_state->vulkan.batch_state.current_batch_index];
        } else {
            // 如果没有启用批处理渲染，扩展当前批次
            int new_capacity = current_batch->command_capacity * 2;
            DrawCommand* new_commands = 
                (DrawCommand*)safe_realloc(current_batch->commands, 
                                         new_capacity * sizeof(DrawCommand));
            if (!new_commands) {
                return COMPOSITOR_ERROR_OUT_OF_MEMORY;
            }
            current_batch->commands = new_commands;
            current_batch->command_capacity = new_capacity;
        }
    }
    
    // 添加命令到批次
    current_batch->commands[current_batch->command_count] = *command;
    current_batch->command_count++;
    
    // 更新统计信息
    g_compositor_state->vulkan.optimization_stats.commands_added++;
    
    return COMPOSITOR_OK;
}

// 执行批次命令
void execute_batch_commands(VkCommandBuffer command_buffer, RenderBatch* batch) {
    if (!batch || batch->command_count == 0) {
        return;
    }
    
    compositor_perf_start_measurement(COMPOSITOR_PERF_BATCH_EXECUTION);
    
    log_message(COMPOSITOR_LOG_DEBUG, "Executing batch with %d commands", batch->command_count);
    
    // 这里实现批次命令执行逻辑
    for (int i = 0; i < batch->command_count; i++) {
        // 执行单个绘制命令
        execute_draw_command(command_buffer, &batch->commands[i]);
    }
    
    compositor_perf_end_measurement(COMPOSITOR_PERF_BATCH_EXECUTION);
}

// 执行单个绘制命令
void execute_draw_command(VkCommandBuffer command_buffer, DrawCommand* command) {
    if (!command) {
        return;
    }
    
    // 这里实现单个绘制命令执行逻辑
    switch (command->type) {
        case DRAW_COMMAND_TEXTURE:
            // 绘制纹理
            break;
        case DRAW_COMMAND_QUAD:
            // 绘制四边形
            break;
        case DRAW_COMMAND_RECTANGLE:
            // 绘制矩形
            break;
        default:
            log_message(COMPOSITOR_LOG_WARN, "Unknown draw command type: %d", command->type);
            break;
    }
}

// 执行所有渲染批次
int execute_all_batches(VkCommandBuffer command_buffer) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    compositor_perf_start_measurement(COMPOSITOR_PERF_ALL_BATCHES_EXECUTION);
    
    log_message(COMPOSITOR_LOG_DEBUG, "Executing all %d batches", 
               g_compositor_state->vulkan.batch_state.batch_count);
    
    // 执行所有批次
    for (int i = 0; i < g_compositor_state->vulkan.batch_state.batch_count; i++) {
        RenderBatch* batch = &g_compositor_state->vulkan.batch_state.batches[i];
        if (batch->command_count > 0) {
            execute_batch_commands(command_buffer, batch);
        }
    }
    
    // 清理所有批次
    g_compositor_state->vulkan.batch_state.batch_count = 0;
    g_compositor_state->vulkan.batch_state.current_batch_index = -1;
    
    compositor_perf_end_measurement(COMPOSITOR_PERF_ALL_BATCHES_EXECUTION);
    return COMPOSITOR_OK;
}

// 获取渲染优化统计信息
void get_render_optimization_stats(RenderOptimizationStats* stats) {
    if (!g_compositor_state || !stats) {
        return;
    }
    
    *stats = g_compositor_state->vulkan.optimization_stats;
    
    // 更新运行时间
    stats->runtime_ms = get_current_time_ms() - stats->start_time;
    
    // 计算平均批次大小
    if (stats->batches_executed > 0) {
        stats->avg_batch_size = (
            float)stats->commands_executed / stats->batches_executed;
    } else {
        stats->avg_batch_size = 0.0f;
    }
}