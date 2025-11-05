/*
 * WinDroids Compositor - Dirty Region Management
 * Implementation of dirty rectangle tracking and optimization
 */

#include "compositor_dirty.h"
#include "compositor_utils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// 全局状态引用
static CompositorState* g_state = NULL;

// 设置合成器状态引用（内部使用）
void compositor_dirty_set_state(CompositorState* state) {
    g_state = state;
}

// 标记脏区域
void compositor_mark_dirty_rect(int x, int y, int width, int height) {
    if (!g_state || !g_state->use_dirty_rect_optimization || 
        !g_state->dirty_rects || width <= 0 || height <= 0) {
        return;
    }
    
    // 限制脏区域在屏幕范围内
    x = MAX(x, 0);
    y = MAX(y, 0);
    width = MIN(width, g_state->width - x);
    height = MIN(height, g_state->height - y);
    
    // 检查是否达到最大脏区域数量
    if (g_state->dirty_rect_count >= g_state->config.max_dirty_rects) {
        // 如果达到上限，重置为全屏重绘
        g_state->dirty_rect_count = 1;
        g_state->dirty_rects[0].x = 0;
        g_state->dirty_rects[0].y = 0;
        g_state->dirty_rects[0].width = g_state->width;
        g_state->dirty_rects[0].height = g_state->height;
        return;
    }
    
    // 添加脏区域
    g_state->dirty_rects[g_state->dirty_rect_count].x = x;
    g_state->dirty_rects[g_state->dirty_rect_count].y = y;
    g_state->dirty_rects[g_state->dirty_rect_count].width = width;
    g_state->dirty_rects[g_state->dirty_rect_count].height = height;
    g_state->dirty_rect_count++;
    
    // 合并脏区域
    if (g_state->dirty_rect_count >= 4) { // 当脏区域数量较多时进行合并
        merge_dirty_rects(g_state);
    }
}

// 清除脏区域
void compositor_clear_dirty_rects(void) {
    if (!g_state) return;
    
    g_state->dirty_rect_count = 0;
}

// 合并脏区域（减少渲染区域）
void merge_dirty_rects(CompositorState* state) {
    if (!state || !state->dirty_rects || state->dirty_rect_count <= 1) return;
    
    // 简单的脏区域合并算法
    // 实际实现中可以使用更复杂的合并策略
    int merged_count = 0;
    for (int i = 0; i < state->dirty_rect_count; i++) {
        bool merged = false;
        for (int j = i + 1; j < state->dirty_rect_count; j++) {
            // 检查两个脏区域是否重叠或相邻
            if (state->dirty_rects[i].x < state->dirty_rects[j].x + state->dirty_rects[j].width &&
                state->dirty_rects[i].x + state->dirty_rects[i].width > state->dirty_rects[j].x &&
                state->dirty_rects[i].y < state->dirty_rects[j].y + state->dirty_rects[j].height &&
                state->dirty_rects[i].y + state->dirty_rects[i].height > state->dirty_rects[j].y) {
                // 合并区域
                int min_x = MIN(state->dirty_rects[i].x, state->dirty_rects[j].x);
                int min_y = MIN(state->dirty_rects[i].y, state->dirty_rects[j].y);
                int max_x = MAX(state->dirty_rects[i].x + state->dirty_rects[i].width, 
                               state->dirty_rects[j].x + state->dirty_rects[j].width);
                int max_y = MAX(state->dirty_rects[i].y + state->dirty_rects[i].height, 
                               state->dirty_rects[j].y + state->dirty_rects[j].height);
                
                state->dirty_rects[i].x = min_x;
                state->dirty_rects[i].y = min_y;
                state->dirty_rects[i].width = max_x - min_x;
                state->dirty_rects[i].height = max_y - min_y;
                
                // 移除已合并的区域
                if (j < state->dirty_rect_count - 1) {
                    state->dirty_rects[j] = state->dirty_rects[state->dirty_rect_count - 1];
                }
                state->dirty_rect_count--;
                j--; // 重新检查当前位置
                merged = true;
            }
        }
        if (!merged) {
            merged_count++;
        }
    }
    
    if (merged_count > 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "Merged dirty rects: %d -> %d", 
                   merged_count, state->dirty_rect_count);
    }
}