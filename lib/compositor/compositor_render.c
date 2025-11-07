#include "compositor_render.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>

#define LOG_TAG "Renderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 全局渲染器状态
static struct renderer g_renderer = {0};
static int g_screen_width = 0;
static int g_screen_height = 0;

// 渲染状态缓存
static struct {
    uint32_t current_texture;
    float current_opacity;
    bool current_blend_enabled;
    uint32_t current_shader;
    bool dirty;
} g_render_state_cache = {0};

// 内部函数声明
static void renderer_update_stats(void);
static uint64_t renderer_get_time(void);
static void renderer_merge_dirty_regions(void);
static void renderer_apply_state_cache(void);
static void renderer_set_texture(uint32_t texture);
static void renderer_set_opacity(float opacity);
static void renderer_set_blend_enabled(bool enabled);
static void renderer_set_shader(uint32_t shader);

// 初始化渲染器
int renderer_init(int screen_width, int screen_height) {
    if (g_renderer.layers[0].targets) {
        LOGE("Renderer already initialized");
        return -1;
    }
    
    memset(&g_renderer, 0, sizeof(g_renderer));
    g_screen_width = screen_width;
    g_screen_height = screen_height;
    g_renderer.next_target_id = 1;
    g_renderer.vsync_enabled = true;
    g_renderer.max_fps = 60;
    g_renderer.target_fps = 60;
    g_renderer.dirty_regions_enabled = true;
    g_renderer.multithreading_enabled = false;
    g_renderer.last_frame_time = renderer_get_time();
    
    // 初始化渲染层
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        g_renderer.layers[i].type = (render_layer_type_t)i;
        g_renderer.layers[i].visible = true;
        g_renderer.layers[i].opacity = 1.0f;
    }
    
    LOGI("Renderer initialized with screen size %dx%d", screen_width, screen_height);
    return 0;
}

// 销毁渲染器
void renderer_destroy(void) {
    // 销毁所有渲染目标
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        struct render_layer* layer = &g_renderer.layers[i];
        for (uint32_t j = 0; j < layer->target_count; j++) {
            renderer_destroy_target(&layer->targets[j]);
        }
        free(layer->targets);
        layer->targets = NULL;
        layer->target_count = 0;
    }
    
    memset(&g_renderer, 0, sizeof(g_renderer));
    LOGI("Renderer destroyed");
}

// 创建渲染目标
struct render_target* renderer_create_target(int width, int height) {
    struct render_target* target = (struct render_target*)calloc(1, sizeof(struct render_target));
    if (!target) {
        LOGE("Failed to allocate memory for render target");
        return NULL;
    }
    
    target->id = g_renderer.next_target_id++;
    target->width = width;
    target->height = height;
    target->texture = 0; // 实际实现中应创建纹理
    target->dirty = true;
    
    LOGI("Created render target %d with size %dx%d", target->id, width, height);
    return target;
}

// 销毁渲染目标
void renderer_destroy_target(struct render_target* target) {
    if (!target) return;
    
    // 清除脏区域
    struct dirty_region* region = target->dirty_regions;
    while (region) {
        struct dirty_region* next = region->next;
        free(region);
        region = next;
    }
    
    // 实际实现中应销毁纹理
    if (target->texture) {
        // glDeleteTextures(1, &target->texture);
    }
    
    LOGI("Destroyed render target %d", target->id);
    free(target);
}

// 添加渲染目标到层
int renderer_add_target_to_layer(struct render_target* target, render_layer_type_t layer) {
    if (!target || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        LOGE("Invalid target or layer");
        return -1;
    }
    
    struct render_layer* l = &g_renderer.layers[layer];
    
    // 检查目标是否已在层中
    for (uint32_t i = 0; i < l->target_count; i++) {
        if (l->targets[i].id == target->id) {
            LOGE("Target %d already in layer %d", target->id, layer);
            return -1;
        }
    }
    
    // 扩展目标数组
    struct render_target* new_targets = (struct render_target*)realloc(
        l->targets, (l->target_count + 1) * sizeof(struct render_target));
    if (!new_targets) {
        LOGE("Failed to expand target array");
        return -1;
    }
    
    l->targets = new_targets;
    l->targets[l->target_count] = *target;
    l->target_count++;
    
    LOGI("Added target %d to layer %d", target->id, layer);
    return 0;
}

// 从层中移除渲染目标
int renderer_remove_target_from_layer(struct render_target* target, render_layer_type_t layer) {
    if (!target || layer < 0 || layer >= RENDER_LAYER_COUNT) {
        LOGE("Invalid target or layer");
        return -1;
    }
    
    struct render_layer* l = &g_renderer.layers[layer];
    
    // 查找目标
    uint32_t index = UINT32_MAX;
    for (uint32_t i = 0; i < l->target_count; i++) {
        if (l->targets[i].id == target->id) {
            index = i;
            break;
        }
    }
    
    if (index == UINT32_MAX) {
        LOGE("Target %d not found in layer %d", target->id, layer);
        return -1;
    }
    
    // 移动后续目标
    for (uint32_t i = index; i < l->target_count - 1; i++) {
        l->targets[i] = l->targets[i + 1];
    }
    
    l->target_count--;
    
    LOGI("Removed target %d from layer %d", target->id, layer);
    return 0;
}

// 标记区域为脏
void renderer_mark_dirty(int x, int y, int width, int height) {
    if (!g_renderer.dirty_regions_enabled) {
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
    if (x + width > g_screen_width) {
        width = g_screen_width - x;
    }
    if (y + height > g_screen_height) {
        height = g_screen_height - y;
    }
    
    if (width <= 0 || height <= 0) {
        return;
    }
    
    // 创建新的脏区域
    struct dirty_region* region = (struct dirty_region*)malloc(sizeof(struct dirty_region));
    if (!region) {
        LOGE("Failed to allocate memory for dirty region");
        return;
    }
    
    region->x = x;
    region->y = y;
    region->width = width;
    region->height = height;
    
    // 添加到所有可见层的脏区域列表
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        if (g_renderer.layers[i].visible) {
            // 为每个层创建一个脏区域副本
            if (i == 0) {
                region->next = g_renderer.layers[i].dirty_regions;
                g_renderer.layers[i].dirty_regions = region;
            } else {
                struct dirty_region* copy = (struct dirty_region*)malloc(sizeof(struct dirty_region));
                if (copy) {
                    *copy = *region;
                    copy->next = g_renderer.layers[i].dirty_regions;
                    g_renderer.layers[i].dirty_regions = copy;
                }
            }
        }
    }
}

// 标记目标为脏
void renderer_mark_target_dirty(struct render_target* target) {
    if (!target) return;
    
    target->dirty = true;
    
    // 标记整个目标区域为脏
    renderer_mark_dirty(0, 0, target->width, target->height);
}

// 清除脏区域
void renderer_clear_dirty_regions(void) {
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        struct dirty_region* region = g_renderer.layers[i].dirty_regions;
        while (region) {
            struct dirty_region* next = region->next;
            free(region);
            region = next;
        }
        g_renderer.layers[i].dirty_regions = NULL;
    }
}

// 设置层可见性
void renderer_set_layer_visibility(render_layer_type_t layer, bool visible) {
    if (layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    g_renderer.layers[layer].visible = visible;
}

// 设置层不透明度
void renderer_set_layer_opacity(render_layer_type_t layer, float opacity) {
    if (layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return;
    }
    
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    
    g_renderer.layers[layer].opacity = opacity;
}

// 设置垂直同步
void renderer_set_vsync(bool enabled) {
    g_renderer.vsync_enabled = enabled;
    // 实际实现中应设置OpenGL/Vulkan的垂直同步
}

// 设置最大帧率
void renderer_set_max_fps(uint32_t fps) {
    if (fps == 0) fps = 60;
    g_renderer.max_fps = fps;
}

// 设置目标帧率
void renderer_set_target_fps(uint32_t fps) {
    if (fps == 0) fps = 60;
    g_renderer.target_fps = fps;
}

// 启用/禁用脏区域优化
void renderer_set_dirty_regions_enabled(bool enabled) {
    g_renderer.dirty_regions_enabled = enabled;
}

// 启用/禁用多线程渲染
void renderer_set_multithreading_enabled(bool enabled) {
    g_renderer.multithreading_enabled = enabled;
    // 实际实现中应创建/销毁渲染线程
}

// 开始渲染帧
int renderer_begin_frame(void) {
    // 帧率控制
    uint64_t current_time = renderer_get_time();
    uint64_t frame_time = 1000000 / g_renderer.target_fps; // 微秒
    
    if (current_time - g_renderer.last_frame_time < frame_time) {
        // 等待到下一帧时间
        // 实际实现中应使用条件变量或类似机制
    }
    
    g_renderer.last_frame_time = current_time;
    
    // 重置统计
    g_renderer.stats.draw_calls = 0;
    g_renderer.stats.triangles = 0;
    g_renderer.stats.texture_switches = 0;
    
    // 合并脏区域
    if (g_renderer.dirty_regions_enabled) {
        renderer_merge_dirty_regions();
    }
    
    return 0;
}

// 结束渲染帧
int renderer_end_frame(void) {
    // 清除脏区域
    if (g_renderer.dirty_regions_enabled) {
        renderer_clear_dirty_regions();
    }
    
    // 更新统计
    renderer_update_stats();
    
    return 0;
}

// 渲染层
int renderer_render_layer(render_layer_type_t layer) {
    if (layer < 0 || layer >= RENDER_LAYER_COUNT) {
        return -1;
    }
    
    struct render_layer* l = &g_renderer.layers[layer];
    
    if (!l->visible || l->opacity <= 0.0f) {
        return 0;
    }
    
    // 设置层状态
    // 实际实现中应设置OpenGL/Vulkan状态
    
    // 渲染所有目标
    for (uint32_t i = 0; i < l->target_count; i++) {
        struct render_target* target = &l->targets[i];
        
        if (target->dirty || !g_renderer.dirty_regions_enabled) {
            renderer_render_target(target);
            target->dirty = false;
        }
    }
    
    return 0;
}

// 渲染目标
int renderer_render_target(struct render_target* target) {
    if (!target) {
        return -1;
    }
    
    // 使用状态缓存设置纹理
    renderer_set_texture(target->texture);
    
    // 设置层的不透明度
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        for (uint32_t j = 0; j < g_renderer.layers[i].target_count; j++) {
            if (g_renderer.layers[i].targets[j].id == target->id) {
                renderer_set_opacity(g_renderer.layers[i].opacity);
                renderer_set_blend_enabled(g_renderer.layers[i].opacity < 1.0f);
                break;
            }
        }
    }
    
    // 应用状态缓存
    renderer_apply_state_cache();
    
    // 渲染目标
    // 实际实现中应执行绘制调用
    g_renderer.stats.draw_calls++;
    g_renderer.stats.triangles += 2; // 假设是一个四边形
    
    return 0;
}

// 获取渲染统计
void renderer_get_stats(struct render_stats* stats) {
    if (!stats) return;
    
    *stats = g_renderer.stats;
}

// 重置渲染统计
void renderer_reset_stats(void) {
    memset(&g_renderer.stats, 0, sizeof(g_renderer.stats));
}

// 应用状态缓存
static void renderer_apply_state_cache(void) {
    if (!g_render_state_cache.dirty) {
        return;
    }
    
    // 实际实现中应应用OpenGL/Vulkan状态
    // 这里只是模拟状态应用
    
    g_render_state_cache.dirty = false;
}

// 设置纹理
static void renderer_set_texture(uint32_t texture) {
    if (g_render_state_cache.current_texture != texture) {
        g_render_state_cache.current_texture = texture;
        g_render_state_cache.dirty = true;
        g_renderer.stats.texture_switches++;
    }
}

// 设置不透明度
static void renderer_set_opacity(float opacity) {
    if (g_render_state_cache.current_opacity != opacity) {
        g_render_state_cache.current_opacity = opacity;
        g_render_state_cache.dirty = true;
    }
}

// 设置混合模式
static void renderer_set_blend_enabled(bool enabled) {
    if (g_render_state_cache.current_blend_enabled != enabled) {
        g_render_state_cache.current_blend_enabled = enabled;
        g_render_state_cache.dirty = true;
    }
}

// 设置着色器
static void renderer_set_shader(uint32_t shader) {
    if (g_render_state_cache.current_shader != shader) {
        g_render_state_cache.current_shader = shader;
        g_render_state_cache.dirty = true;
    }
}

// 更新渲染器（每帧调用）
void renderer_update(void) {
    // 这里可以添加渲染器的更新逻辑
    // 例如资源管理、状态更新等
}

// 内部函数：更新统计
static void renderer_update_stats(void) {
    g_renderer.stats.frame_count++;
    
    // 每秒更新一次FPS
    static uint64_t last_fps_update = 0;
    static uint32_t last_frame_count = 0;
    
    uint64_t current_time = renderer_get_time();
    if (current_time - last_fps_update >= 1000000) { // 1秒
        g_renderer.stats.fps = (float)(g_renderer.stats.frame_count - last_frame_count);
        last_fps_update = current_time;
        last_frame_count = g_renderer.stats.frame_count;
    }
}

// 内部函数：获取当前时间（微秒）
static uint64_t renderer_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

// 内部函数：合并脏区域
static void renderer_merge_dirty_regions(void) {
    for (int i = 0; i < RENDER_LAYER_COUNT; i++) {
        struct render_layer* layer = &g_renderer.layers[i];
        
        if (!layer->visible) {
            continue;
        }
        
        // 简化实现：合并所有脏区域为一个大的脏区域
        struct dirty_region* region = layer->dirty_regions;
        if (!region) {
            continue;
        }
        
        int min_x = region->x;
        int min_y = region->y;
        int max_x = region->x + region->width;
        int max_y = region->y + region->height;
        
        while (region) {
            if (region->x < min_x) min_x = region->x;
            if (region->y < min_y) min_y = region->y;
            if (region->x + region->width > max_x) max_x = region->x + region->width;
            if (region->y + region->height > max_y) max_y = region->y + region->height;
            
            struct dirty_region* next = region->next;
            free(region);
            region = next;
        }
        
        // 创建合并后的脏区域
        struct dirty_region* merged = (struct dirty_region*)malloc(sizeof(struct dirty_region));
        if (merged) {
            merged->x = min_x;
            merged->y = min_y;
            merged->width = max_x - min_x;
            merged->height = max_y - min_y;
            merged->next = NULL;
            layer->dirty_regions = merged;
        }
    }
}