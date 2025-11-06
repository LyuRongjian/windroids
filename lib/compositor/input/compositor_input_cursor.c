#include "compositor_input_cursor.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 全局状态指针
static void* g_compositor_state = NULL;

// 鼠标光标状态
static struct {
    CompositorCursor cursor;           // 当前光标
    bool initialized;                  // 是否已初始化
    float last_move_time;              // 上次移动时间
    bool auto_hide_active;             // 自动隐藏是否激活
    float hide_timer;                  // 隐藏计时器
    float acceleration_factor;         // 加速因子
    float velocity_x, velocity_y;      // 速度分量
    char* theme_name;                  // 主题名称
    uint32_t* theme_cursors[30];       // 主题光标数据
    int32_t theme_cursor_sizes[30];    // 主题光标大小
    bool theme_loaded;                 // 主题是否已加载
} g_cursor_state = {
    .initialized = false,
    .last_move_time = 0.0f,
    .auto_hide_active = false,
    .hide_timer = 0.0f,
    .acceleration_factor = 1.0f,
    .velocity_x = 0.0f,
    .velocity_y = 0.0f,
    .theme_name = NULL,
    .theme_cursors = {NULL},
    .theme_cursor_sizes = {0},
    .theme_loaded = false
};

// 设置合成器状态引用
void compositor_cursor_set_state(void* state) {
    g_compositor_state = state;
}

// 初始化鼠标光标系统
int compositor_cursor_init(void) {
    if (g_cursor_state.initialized) {
        return -1; // 已初始化错误
    }
    
    // 初始化光标结构
    memset(&g_cursor_state.cursor, 0, sizeof(CompositorCursor));
    g_cursor_state.cursor.type = COMPOSITOR_CURSOR_DEFAULT;
    g_cursor_state.cursor.x = 0;
    g_cursor_state.cursor.y = 0;
    g_cursor_state.cursor.hotspot_x = 0;
    g_cursor_state.cursor.hotspot_y = 0;
    g_cursor_state.cursor.visible = true;
    g_cursor_state.cursor.animated = false;
    g_cursor_state.cursor.animation_time = 0.0f;
    g_cursor_state.cursor.custom_data = NULL;
    g_cursor_state.cursor.width = 32;
    g_cursor_state.cursor.height = 32;
    
    // 分配像素数据
    g_cursor_state.cursor.pixels = (uint32_t*)calloc(g_cursor_state.cursor.width * g_cursor_state.cursor.height, sizeof(uint32_t));
    if (!g_cursor_state.cursor.pixels) {
        return -2; // 内存不足错误
    }
    
    // 初始化其他状态
    g_cursor_state.last_move_time = 0.0f;
    g_cursor_state.auto_hide_active = false;
    g_cursor_state.hide_timer = 0.0f;
    g_cursor_state.acceleration_factor = 1.0f;
    g_cursor_state.velocity_x = 0.0f;
    g_cursor_state.velocity_y = 0.0f;
    g_cursor_state.theme_name = NULL;
    g_cursor_state.theme_loaded = false;
    
    // 加载默认光标主题
    compositor_cursor_load_theme("default");
    
    g_cursor_state.initialized = true;
    return 0; // 成功
}

// 清理鼠标光标系统
void compositor_cursor_cleanup(void) {
    if (!g_cursor_state.initialized) {
        return;
    }
    
    // 释放像素数据
    if (g_cursor_state.cursor.pixels) {
        free(g_cursor_state.cursor.pixels);
        g_cursor_state.cursor.pixels = NULL;
    }
    
    // 释放主题名称
    if (g_cursor_state.theme_name) {
        free(g_cursor_state.theme_name);
        g_cursor_state.theme_name = NULL;
    }
    
    // 释放主题光标数据
    for (int i = 0; i < 30; i++) {
        if (g_cursor_state.theme_cursors[i]) {
            free(g_cursor_state.theme_cursors[i]);
            g_cursor_state.theme_cursors[i] = NULL;
        }
    }
    
    g_cursor_state.initialized = false;
}

// 设置光标类型
int compositor_cursor_set_type(CompositorCursorType type) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    if (type < COMPOSITOR_CURSOR_DEFAULT || type > COMPOSITOR_CURSOR_CUSTOM) {
        return -2; // 无效参数错误
    }
    
    g_cursor_state.cursor.type = type;
    
    // 如果是主题光标且已加载主题，则更新光标数据
    if (type < COMPOSITOR_CURSOR_CUSTOM && g_cursor_state.theme_loaded && 
        g_cursor_state.theme_cursors[type] && g_cursor_state.theme_cursor_sizes[type] > 0) {
        
        // 重新分配像素数据
        int32_t size = g_cursor_state.theme_cursor_sizes[type];
        uint32_t* new_pixels = (uint32_t*)realloc(g_cursor_state.cursor.pixels, size * size * sizeof(uint32_t));
        if (!new_pixels) {
            return -2; // 内存不足错误
        }
        
        g_cursor_state.cursor.pixels = new_pixels;
        g_cursor_state.cursor.width = size;
        g_cursor_state.cursor.height = size;
        
        // 复制主题光标数据
        memcpy(g_cursor_state.cursor.pixels, g_cursor_state.theme_cursors[type], 
               size * size * sizeof(uint32_t));
    }
    
    // 标记光标区域需要重绘
    // 这里应该调用合成器的标记脏区域函数
    // 例如: compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
    //                             g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
    //                             g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    
    return 0; // 成功
}

// 获取光标类型
CompositorCursorType compositor_cursor_get_type(void) {
    if (!g_cursor_state.initialized) {
        return COMPOSITOR_CURSOR_DEFAULT;
    }
    
    return g_cursor_state.cursor.type;
}

// 设置光标位置
int compositor_cursor_set_position(int32_t x, int32_t y) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    // 边界检查
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    // 这里应该从合成器状态获取屏幕尺寸
    // if (x >= screen_width) x = screen_width - 1;
    // if (y >= screen_height) y = screen_height - 1;
    
    // 标记旧位置需要重绘
    // 这里应该调用合成器的标记脏区域函数
    // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
    //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
    //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    
    // 计算速度和加速度
    float current_time = get_current_time_ms() / 1000.0f;
    if (g_cursor_state.last_move_time > 0.0f) {
        float time_delta = current_time - g_cursor_state.last_move_time;
        if (time_delta > 0.0f) {
            g_cursor_state.velocity_x = (x - g_cursor_state.cursor.x) / time_delta;
            g_cursor_state.velocity_y = (y - g_cursor_state.cursor.y) / time_delta;
        }
    }
    g_cursor_state.last_move_time = current_time;
    
    // 更新位置
    g_cursor_state.cursor.x = x;
    g_cursor_state.cursor.y = y;
    
    // 重置自动隐藏计时器
    if (g_cursor_state.auto_hide_active) {
        g_cursor_state.hide_timer = 0.0f;
        if (!g_cursor_state.cursor.visible) {
            g_cursor_state.cursor.visible = true;
        }
    }
    
    // 标记新位置需要重绘
    // 这里应该调用合成器的标记脏区域函数
    // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
    //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
    //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    
    return 0; // 成功
}

// 获取光标位置
void compositor_cursor_get_position(int32_t* x, int32_t* y) {
    if (!g_cursor_state.initialized) {
        if (x) *x = 0;
        if (y) *y = 0;
        return;
    }
    
    if (x) *x = g_cursor_state.cursor.x;
    if (y) *y = g_cursor_state.cursor.y;
}

// 设置光标可见性
int compositor_cursor_set_visibility(bool visible) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    if (g_cursor_state.cursor.visible != visible) {
        g_cursor_state.cursor.visible = visible;
        
        // 标记光标区域需要重绘
        // 这里应该调用合成器的标记脏区域函数
        // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
        //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
        //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    }
    
    return 0; // 成功
}

// 获取光标可见性
bool compositor_cursor_is_visible(void) {
    if (!g_cursor_state.initialized) {
        return false;
    }
    
    return g_cursor_state.cursor.visible;
}

// 设置光标热点
int compositor_cursor_set_hotspot(int32_t x, int32_t y) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    // 标记旧位置需要重绘
    // 这里应该调用合成器的标记脏区域函数
    // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
    //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
    //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    
    g_cursor_state.cursor.hotspot_x = x;
    g_cursor_state.cursor.hotspot_y = y;
    
    // 标记新位置需要重绘
    // 这里应该调用合成器的标记脏区域函数
    // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
    //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
    //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    
    return 0; // 成功
}

// 获取光标热点
void compositor_cursor_get_hotspot(int32_t* x, int32_t* y) {
    if (!g_cursor_state.initialized) {
        if (x) *x = 0;
        if (y) *y = 0;
        return;
    }
    
    if (x) *x = g_cursor_state.cursor.hotspot_x;
    if (y) *y = g_cursor_state.cursor.hotspot_y;
}

// 设置自定义数据
int compositor_cursor_set_custom_data(void* data) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    g_cursor_state.cursor.custom_data = data;
    return 0; // 成功
}

// 获取自定义数据
void* compositor_cursor_get_custom_data(void) {
    if (!g_cursor_state.initialized) {
        return NULL;
    }
    
    return g_cursor_state.cursor.custom_data;
}

// 设置动画状态
int compositor_cursor_set_animated(bool animated) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    g_cursor_state.cursor.animated = animated;
    if (!animated) {
        g_cursor_state.cursor.animation_time = 0.0f;
    }
    
    return 0; // 成功
}

// 获取动画状态
bool compositor_cursor_is_animated(void) {
    if (!g_cursor_state.initialized) {
        return false;
    }
    
    return g_cursor_state.cursor.animated;
}

// 设置光标大小
int compositor_cursor_set_size(int32_t width, int32_t height) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    if (width <= 0 || height <= 0) {
        return -2; // 无效参数错误
    }
    
    // 重新分配像素数据
    uint32_t* new_pixels = (uint32_t*)realloc(g_cursor_state.cursor.pixels, width * height * sizeof(uint32_t));
    if (!new_pixels) {
        return -2; // 内存不足错误
    }
    
    // 标记旧位置需要重绘
    // 这里应该调用合成器的标记脏区域函数
    // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
    //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
    //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    
    g_cursor_state.cursor.pixels = new_pixels;
    g_cursor_state.cursor.width = width;
    g_cursor_state.cursor.height = height;
    
    // 标记新位置需要重绘
    // 这里应该调用合成器的标记脏区域函数
    // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
    //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
    //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    
    return 0; // 成功
}

// 获取光标大小
void compositor_cursor_get_size(int32_t* width, int32_t* height) {
    if (!g_cursor_state.initialized) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    
    if (width) *width = g_cursor_state.cursor.width;
    if (height) *height = g_cursor_state.cursor.height;
}

// 设置光标像素数据
int compositor_cursor_set_pixels(const uint32_t* pixels) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    if (!pixels) {
        return -2; // 无效参数错误
    }
    
    // 复制像素数据
    memcpy(g_cursor_state.cursor.pixels, pixels, 
           g_cursor_state.cursor.width * g_cursor_state.cursor.height * sizeof(uint32_t));
    
    // 标记光标区域需要重绘
    // 这里应该调用合成器的标记脏区域函数
    // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
    //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
    //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    
    return 0; // 成功
}

// 获取光标像素数据
const uint32_t* compositor_cursor_get_pixels(void) {
    if (!g_cursor_state.initialized) {
        return NULL;
    }
    
    return g_cursor_state.cursor.pixels;
}

// 更新光标动画
int compositor_cursor_update(float delta_time) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    // 更新动画时间
    if (g_cursor_state.cursor.animated) {
        g_cursor_state.cursor.animation_time += delta_time;
        
        // 标记光标区域需要重绘
        // 这里应该调用合成器的标记脏区域函数
        // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
        //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
        //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
    }
    
    // 更新自动隐藏计时器
    if (g_cursor_state.auto_hide_active && g_cursor_state.cursor.visible) {
        g_cursor_state.hide_timer += delta_time;
        
        // 检查是否需要隐藏光标
        // 这里应该从合成器配置获取隐藏超时时间
        // int32_t hide_timeout = g_compositor_state->config.cursor_hide_timeout;
        int32_t hide_timeout = 3000; // 默认3秒
        
        if (g_cursor_state.hide_timer >= hide_timeout / 1000.0f) {
            g_cursor_state.cursor.visible = false;
            
            // 标记光标区域需要重绘
            // 这里应该调用合成器的标记脏区域函数
            // compositor_mark_dirty(g_cursor_state.cursor.x - g_cursor_state.cursor.hotspot_x, 
            //                      g_cursor_state.cursor.y - g_cursor_state.cursor.hotspot_y,
            //                      g_cursor_state.cursor.width, g_cursor_state.cursor.height);
        }
    }
    
    return 0; // 成功
}

// 加载光标主题
int compositor_cursor_load_theme(const char* theme_name) {
    if (!theme_name) {
        return -2; // 无效参数错误
    }
    
    // 释放旧主题名称
    if (g_cursor_state.theme_name) {
        free(g_cursor_state.theme_name);
        g_cursor_state.theme_name = NULL;
    }
    
    // 释放旧主题光标数据
    for (int i = 0; i < 30; i++) {
        if (g_cursor_state.theme_cursors[i]) {
            free(g_cursor_state.theme_cursors[i]);
            g_cursor_state.theme_cursors[i] = NULL;
        }
        g_cursor_state.theme_cursor_sizes[i] = 0;
    }
    
    // 复制新主题名称
    g_cursor_state.theme_name = strdup(theme_name);
    if (!g_cursor_state.theme_name) {
        return -2; // 内存不足错误
    }
    
    // 这里应该实现实际的主题加载逻辑
    // 为了简化，我们只生成一些默认光标
    
    // 生成默认光标
    int32_t default_size = 32;
    uint32_t* default_pixels = (uint32_t*)calloc(default_size * default_size, sizeof(uint32_t));
    if (default_pixels) {
        // 简单生成一个白色箭头光标
        for (int y = 0; y < default_size; y++) {
            for (int x = 0; x < default_size; x++) {
                if (y == x && x < 16) {
                    default_pixels[y * default_size + x] = 0xFFFFFFFF; // 白色
                } else if (y == x + 1 && x < 15) {
                    default_pixels[y * default_size + x] = 0xFFFFFFFF; // 白色
                } else {
                    default_pixels[y * default_size + x] = 0x00000000; // 透明
                }
            }
        }
        
        // 设置为默认光标
        g_cursor_state.theme_cursors[COMPOSITOR_CURSOR_DEFAULT] = default_pixels;
        g_cursor_state.theme_cursor_sizes[COMPOSITOR_CURSOR_DEFAULT] = default_size;
        
        // 复制到其他光标类型
        for (int i = COMPOSITOR_CURSOR_POINTER; i < COMPOSITOR_CURSOR_CUSTOM; i++) {
            g_cursor_state.theme_cursors[i] = (uint32_t*)malloc(default_size * default_size * sizeof(uint32_t));
            if (g_cursor_state.theme_cursors[i]) {
                memcpy(g_cursor_state.theme_cursors[i], default_pixels, 
                       default_size * default_size * sizeof(uint32_t));
                g_cursor_state.theme_cursor_sizes[i] = default_size;
            }
        }
    }
    
    g_cursor_state.theme_loaded = true;
    return 0; // 成功
}

// 设置光标配置
int compositor_cursor_set_config(bool auto_hide, int32_t hide_timeout, 
                                bool acceleration, float sensitivity, 
                                bool theme_enabled, const char* theme_name, 
                                int32_t size) {
    if (!g_cursor_state.initialized) {
        return -1; // 未初始化错误
    }
    
    if (hide_timeout < 0 || sensitivity <= 0.0f || size <= 0) {
        return -2; // 无效参数错误
    }
    
    // 更新配置
    g_cursor_state.auto_hide_active = auto_hide;
    g_cursor_state.acceleration_factor = acceleration ? sensitivity : 1.0f;
    
    // 更新主题名称
    if (theme_name && (!g_cursor_state.theme_name || 
        strcmp(g_cursor_state.theme_name, theme_name) != 0)) {
        
        // 加载新主题
        if (theme_enabled) {
            compositor_cursor_load_theme(theme_name);
        }
    }
    
    // 更新光标大小
    compositor_cursor_set_size(size, size);
    
    return 0; // 成功
}

// 获取光标配置
void compositor_cursor_get_config(bool* auto_hide, int32_t* hide_timeout, 
                                 bool* acceleration, float* sensitivity, 
                                 bool* theme_enabled, const char** theme_name, 
                                 int32_t* size) {
    if (!g_cursor_state.initialized) {
        if (auto_hide) *auto_hide = false;
        if (hide_timeout) *hide_timeout = 0;
        if (acceleration) *acceleration = false;
        if (sensitivity) *sensitivity = 1.0f;
        if (theme_enabled) *theme_enabled = false;
        if (theme_name) *theme_name = NULL;
        if (size) *size = 32;
        return;
    }
    
    if (auto_hide) *auto_hide = g_cursor_state.auto_hide_active;
    if (hide_timeout) *hide_timeout = 3000; // 默认3秒
    if (acceleration) *acceleration = (g_cursor_state.acceleration_factor != 1.0f);
    if (sensitivity) *sensitivity = g_cursor_state.acceleration_factor;
    if (theme_enabled) *theme_enabled = g_cursor_state.theme_loaded;
    if (theme_name) *theme_name = g_cursor_state.theme_name;
    if (size) *size = g_cursor_state.cursor.width;
}