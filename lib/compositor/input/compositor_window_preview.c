/*
 * WinDroids Compositor
 * Window Preview Rendering Implementation
 */

#include "compositor_window_preview.h"
#include "compositor_vulkan_window.h"
#include "compositor_utils.h"
#include "compositor_render.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// 窗口预览配置
#define PREVIEW_MAX_WINDOWS 9          // 最大预览窗口数量 (3x3网格)
#define PREVIEW_THUMBNAIL_SCALE 0.25f   // 缩略图缩放比例
#define PREVIEW_SPACING 20              // 预览窗口之间的间距
#define PREVIEW_BORDER_WIDTH 2          // 预览窗口边框宽度
#define PREVIEW_SELECTED_SCALE 1.2f     // 选中窗口的缩放比例
#define PREVIEW_ANIMATION_DURATION 200  // 动画持续时间 (毫秒)
#define PREVIEW_BACKGROUND_ALPHA 0.7f   // 背景透明度
#define PREVIEW_TITLE_HEIGHT 24          // 标题栏高度

// 预览窗口状态
typedef struct {
    void* window;               // 原始窗口指针
    bool is_wayland;            // 是否为Wayland窗口
    int x, y;                   // 预览位置
    int width, height;          // 预览尺寸
    float scale;                // 当前缩放比例
    float target_scale;         // 目标缩放比例
    bool is_selected;           // 是否为选中状态
    uint32_t texture_id;        // 纹理ID
    bool texture_valid;         // 纹理是否有效
    char title[256];            // 窗口标题
} PreviewWindow;

// 全局状态
static CompositorState* g_compositor_state = NULL;
static PreviewWindow g_preview_windows[PREVIEW_MAX_WINDOWS];
static int g_preview_window_count = 0;
static int g_selected_preview_index = -1;
static bool g_previews_visible = false;
static uint64_t g_animation_start_time = 0;
static bool g_animation_active = false;

// 设置合成器状态引用
void compositor_window_preview_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 初始化窗口预览系统
int compositor_window_preview_init(void) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 初始化预览窗口状态
    memset(g_preview_windows, 0, sizeof(g_preview_windows));
    g_preview_window_count = 0;
    g_selected_preview_index = -1;
    g_previews_visible = false;
    g_animation_active = false;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Window preview system initialized");
    return COMPOSITOR_OK;
}

// 清理窗口预览系统
void compositor_window_preview_cleanup(void) {
    // 清理预览窗口状态
    memset(g_preview_windows, 0, sizeof(g_preview_windows));
    g_preview_window_count = 0;
    g_selected_preview_index = -1;
    g_previews_visible = false;
    g_animation_active = false;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Window preview system cleaned up");
}

// 计算预览窗口布局
static void calculate_preview_layout(void) {
    if (g_preview_window_count <= 0) {
        return;
    }
    
    // 计算网格布局 (尽量接近正方形)
    int cols = (int)ceil(sqrt(g_preview_window_count));
    int rows = (int)ceil((float)g_preview_window_count / cols);
    
    // 计算每个预览窗口的大小
    int available_width = g_compositor_state->width - PREVIEW_SPACING * (cols + 1);
    int available_height = g_compositor_state->height - PREVIEW_SPACING * (rows + 1);
    
    int cell_width = available_width / cols;
    int cell_height = available_height / rows;
    
    // 计算预览窗口的实际大小 (保持宽高比)
    int preview_width, preview_height;
    
    // 使用第一个窗口的尺寸作为参考
    if (g_preview_window_count > 0) {
        int original_width, original_height;
        if (g_preview_windows[0].is_wayland) {
            WaylandWindow* window = (WaylandWindow*)g_preview_windows[0].window;
            original_width = window->width;
            original_height = window->height;
        } else {
            XwaylandWindowState* window = (XwaylandWindowState*)g_preview_windows[0].window;
            original_width = window->width;
            original_height = window->height;
        }
        
        // 计算缩略图尺寸
        float scale_x = (float)cell_width / original_width * PREVIEW_THUMBNAIL_SCALE;
        float scale_y = (float)cell_height / original_height * PREVIEW_THUMBNAIL_SCALE;
        float scale = fminf(scale_x, scale_y);
        
        preview_width = (int)(original_width * scale);
        preview_height = (int)(original_height * scale);
    } else {
        preview_width = cell_width * PREVIEW_THUMBNAIL_SCALE;
        preview_height = cell_height * PREVIEW_THUMBNAIL_SCALE;
    }
    
    // 计算起始位置 (居中)
    int total_width = preview_width * cols + PREVIEW_SPACING * (cols - 1);
    int total_height = preview_height * rows + PREVIEW_SPACING * (rows - 1);
    int start_x = (g_compositor_state->width - total_width) / 2;
    int start_y = (g_compositor_state->height - total_height) / 2;
    
    // 设置每个预览窗口的位置和大小
    for (int i = 0; i < g_preview_window_count; i++) {
        int row = i / cols;
        int col = i % cols;
        
        g_preview_windows[i].x = start_x + col * (preview_width + PREVIEW_SPACING);
        g_preview_windows[i].y = start_y + row * (preview_height + PREVIEW_SPACING);
        g_preview_windows[i].width = preview_width;
        g_preview_windows[i].height = preview_height;
        
        // 设置选中状态
        g_preview_windows[i].is_selected = (i == g_selected_preview_index);
        g_preview_windows[i].target_scale = g_preview_windows[i].is_selected ? PREVIEW_SELECTED_SCALE : 1.0f;
        
        // 如果是第一次设置，初始化当前缩放比例
        if (g_preview_windows[i].scale == 0.0f) {
            g_preview_windows[i].scale = g_preview_windows[i].target_scale;
        }
    }
}

// 更新动画
static void update_animation(void) {
    if (!g_animation_active) {
        return;
    }
    
    uint64_t current_time = get_current_time_ms();
    uint64_t elapsed = current_time - g_animation_start_time;
    
    if (elapsed >= PREVIEW_ANIMATION_DURATION) {
        // 动画完成
        for (int i = 0; i < g_preview_window_count; i++) {
            g_preview_windows[i].scale = g_preview_windows[i].target_scale;
        }
        g_animation_active = false;
    } else {
        // 计算动画进度 (使用缓动函数)
        float progress = (float)elapsed / PREVIEW_ANIMATION_DURATION;
        progress = 0.5f - 0.5f * cosf(progress * M_PI); // 缓入缓出
        
        // 更新缩放比例
        for (int i = 0; i < g_preview_window_count; i++) {
            float start_scale = g_preview_windows[i].is_selected ? 1.0f : PREVIEW_SELECTED_SCALE;
            float target_scale = g_preview_windows[i].target_scale;
            g_preview_windows[i].scale = start_scale + (target_scale - start_scale) * progress;
        }
    }
}

// 设置窗口预览
int compositor_window_preview_set_windows(void** windows, bool* is_wayland, int count) {
    if (!g_compositor_state || !windows || !is_wayland) {
        return COMPOSITOR_ERROR_INVALID_ARGUMENT;
    }
    
    // 限制预览窗口数量
    g_preview_window_count = (count > PREVIEW_MAX_WINDOWS) ? PREVIEW_MAX_WINDOWS : count;
    
    // 设置预览窗口
    for (int i = 0; i < g_preview_window_count; i++) {
        g_preview_windows[i].window = windows[i];
        g_preview_windows[i].is_wayland = is_wayland[i];
        g_preview_windows[i].texture_id = UINT32_MAX;
        g_preview_windows[i].texture_valid = false;
        
        // 获取窗口标题
        if (is_wayland[i]) {
            WaylandWindow* window = (WaylandWindow*)windows[i];
            strncpy(g_preview_windows[i].title, 
                   window->title ? window->title : "Untitled", 
                   sizeof(g_preview_windows[i].title) - 1);
            g_preview_windows[i].title[sizeof(g_preview_windows[i].title) - 1] = '\0';
        } else {
            XwaylandWindowState* window = (XwaylandWindowState*)windows[i];
            strncpy(g_preview_windows[i].title, 
                   window->title ? window->title : "Untitled", 
                   sizeof(g_preview_windows[i].title) - 1);
            g_preview_windows[i].title[sizeof(g_preview_windows[i].title) - 1] = '\0';
        }
    }
    
    // 计算布局
    calculate_preview_layout();
    
    log_message(COMPOSITOR_LOG_DEBUG, "Set %d windows for preview", g_preview_window_count);
    return COMPOSITOR_OK;
}

// 设置选中的预览窗口
int compositor_window_preview_set_selected(int index) {
    if (index < 0 || index >= g_preview_window_count) {
        return COMPOSITOR_ERROR_INVALID_ARGUMENT;
    }
    
    if (g_selected_preview_index != index) {
        g_selected_preview_index = index;
        
        // 更新选中状态
        for (int i = 0; i < g_preview_window_count; i++) {
            g_preview_windows[i].is_selected = (i == index);
            g_preview_windows[i].target_scale = g_preview_windows[i].is_selected ? PREVIEW_SELECTED_SCALE : 1.0f;
        }
        
        // 启动动画
        g_animation_start_time = get_current_time_ms();
        g_animation_active = true;
    }
    
    return COMPOSITOR_OK;
}

// 显示窗口预览
int compositor_window_preview_show(void) {
    if (g_preview_window_count <= 0) {
        return COMPOSITOR_ERROR_INVALID_STATE;
    }
    
    g_previews_visible = true;
    
    // 启动动画
    g_animation_start_time = get_current_time_ms();
    g_animation_active = true;
    
    log_message(COMPOSITOR_LOG_DEBUG, "Showing window previews");
    return COMPOSITOR_OK;
}

// 隐藏窗口预览
void compositor_window_preview_hide(void) {
    g_previews_visible = false;
    log_message(COMPOSITOR_LOG_DEBUG, "Hiding window previews");
}

// 渲染窗口预览
void compositor_window_preview_render(void) {
    if (!g_compositor_state || !g_previews_visible || g_preview_window_count <= 0) {
        return;
    }
    
    // 更新动画
    update_animation();
    
    // 渲染半透明背景
    // 这里应该调用渲染API绘制半透明背景
    // 例如：render_fullscreen_quad(g_compositor_state, 0.0f, 0.0f, 0.0f, PREVIEW_BACKGROUND_ALPHA);
    
    // 渲染预览窗口
    for (int i = 0; i < g_preview_window_count; i++) {
        PreviewWindow* preview = &g_preview_windows[i];
        
        // 计算实际渲染位置和大小 (考虑缩放)
        int scaled_width = (int)(preview->width * preview->scale);
        int scaled_height = (int)(preview->height * preview->scale);
        int x = preview->x - (scaled_width - preview->width) / 2;
        int y = preview->y - (scaled_height - preview->height) / 2;
        
        // 获取或创建窗口纹理
        if (!preview->texture_valid) {
            void* surface = NULL;
            if (preview->is_wayland) {
                surface = ((WaylandWindow*)preview->window)->surface;
            } else {
                surface = ((XwaylandWindowState*)preview->window)->surface;
            }
            
            if (surface) {
                // 获取纹理ID
                preview->texture_id = get_cached_texture_by_surface(&g_compositor_state->vulkan, surface);
                preview->texture_valid = (preview->texture_id != UINT32_MAX);
            }
        }
        
        // 渲染预览窗口
        if (preview->texture_valid) {
            // 渲染窗口内容
            // 这里应该调用渲染API绘制纹理
            // 例如：render_textured_quad(g_compositor_state, preview->texture_id, x, y, scaled_width, scaled_height);
        }
        
        // 渲染边框
        if (preview->is_selected) {
            // 渲染选中边框
            // 例如：render_border(g_compositor_state, x, y, scaled_width, scaled_height, PREVIEW_BORDER_WIDTH, 1.0f, 1.0f, 1.0f, 1.0f);
        } else {
            // 渲染普通边框
            // 例如：render_border(g_compositor_state, x, y, scaled_width, scaled_height, PREVIEW_BORDER_WIDTH, 0.7f, 0.7f, 0.7f, 1.0f);
        }
        
        // 渲染标题
        // 例如：render_text(g_compositor_state, preview->title, x, y + scaled_height, scaled_width, PREVIEW_TITLE_HEIGHT);
    }
}

// 获取选中的窗口
void* compositor_window_preview_get_selected_window(bool* out_is_wayland) {
    if (g_selected_preview_index < 0 || g_selected_preview_index >= g_preview_window_count) {
        if (out_is_wayland) *out_is_wayland = false;
        return NULL;
    }
    
    if (out_is_wayland) {
        *out_is_wayland = g_preview_windows[g_selected_preview_index].is_wayland;
    }
    
    return g_preview_windows[g_selected_preview_index].window;
}

// 检查预览是否可见
bool compositor_window_preview_is_visible(void) {
    return g_previews_visible;
}

// 获取预览窗口数量
int compositor_window_preview_get_count(void) {
    return g_preview_window_count;
}