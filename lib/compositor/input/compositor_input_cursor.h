#ifndef COMPOSITOR_INPUT_CURSOR_H
#define COMPOSITOR_INPUT_CURSOR_H

#include <stdint.h>
#include <stdbool.h>

// 鼠标光标类型枚举
enum {
    COMPOSITOR_CURSOR_DEFAULT = 0,      // 默认光标
    COMPOSITOR_CURSOR_POINTER = 1,      // 指针光标
    COMPOSITOR_CURSOR_HAND = 2,         // 手形光标
    COMPOSITOR_CURSOR_IBEAM = 3,         // I形光标
    COMPOSITOR_CURSOR_CROSSHAIR = 4,    // 十字光标
    COMPOSITOR_CURSOR_MOVE = 5,         // 移动光标
    COMPOSITOR_CURSOR_RESIZE_N = 6,      // 向上调整大小光标
    COMPOSITOR_CURSOR_RESIZE_S = 7,      // 向下调整大小光标
    COMPOSITOR_CURSOR_RESIZE_E = 8,      // 向右调整大小光标
    COMPOSITOR_CURSOR_RESIZE_W = 9,      // 向左调整大小光标
    COMPOSITOR_CURSOR_RESIZE_NE = 10,    // 向右上调整大小光标
    COMPOSITOR_CURSOR_RESIZE_NW = 11,    // 向左上调整大小光标
    COMPOSITOR_CURSOR_RESIZE_SE = 12,    // 向右下调整大小光标
    COMPOSITOR_CURSOR_RESIZE_SW = 13,    // 向左下调整大小光标
    COMPOSITOR_CURSOR_WAIT = 14,         // 等待光标
    COMPOSITOR_CURSOR_HELP = 15,         // 帮助光标
    COMPOSITOR_CURSOR_FORBIDDEN = 16,    // 禁止光标
    COMPOSITOR_CURSOR_PROGRESS = 17,     // 进度光标
    COMPOSITOR_CURSOR_NO_DROP = 18,      // 不可放置光标
    COMPOSITOR_CURSOR_NOT_ALLOWED = 19,  // 不允许光标
    COMPOSITOR_CURSOR_ALL_SCROLL = 20,   // 全方向滚动光标
    COMPOSITOR_CURSOR_CELL = 21,         // 单元格光标
    COMPOSITOR_CURSOR_VERTICAL_TEXT = 22, // 垂直文本光标
    COMPOSITOR_CURSOR_ALIAS = 23,        // 别名光标
    COMPOSITOR_CURSOR_COPY = 24,         // 复制光标
    COMPOSITOR_CURSOR_ZOOM_IN = 25,      // 放大光标
    COMPOSITOR_CURSOR_ZOOM_OUT = 26,     // 缩小光标
    COMPOSITOR_CURSOR_GRAB = 27,         // 抓取光标
    COMPOSITOR_CURSOR_GRABBING = 28,     // 抓取中光标
    COMPOSITOR_CURSOR_CUSTOM = 29        // 自定义光标
};
typedef int CompositorCursorType;

// 鼠标光标信息结构体
typedef struct {
    CompositorCursorType type;      // 光标类型
    int32_t x, y;                   // 光标位置
    int32_t hotspot_x, hotspot_y;   // 热点位置
    bool visible;                   // 是否可见
    bool animated;                  // 是否动画
    float animation_time;           // 动画时间
    void* custom_data;              // 自定义数据
    int32_t width, height;          // 光标尺寸
    uint32_t* pixels;               // 光标像素数据
} CompositorCursor;

// 鼠标光标配置结构体
typedef struct {
    bool auto_hide;           // 自动隐藏光标
    int32_t hide_timeout;     // 光标隐藏超时（毫秒）
    bool acceleration;       // 启用光标加速
    float sensitivity;        // 光标灵敏度
    bool theme_enabled;       // 启用光标主题
    const char* theme_name;   // 光标主题名称
    int32_t size;             // 光标大小
} CompositorCursorConfig;

// 鼠标光标管理函数
int compositor_cursor_init(void);
void compositor_cursor_cleanup(void);
int compositor_cursor_set_type(CompositorCursorType type);
CompositorCursorType compositor_cursor_get_type(void);
int compositor_cursor_set_position(int32_t x, int32_t y);
void compositor_cursor_get_position(int32_t* x, int32_t* y);
int compositor_cursor_set_visibility(bool visible);
bool compositor_cursor_is_visible(void);
int compositor_cursor_set_hotspot(int32_t x, int32_t y);
void compositor_cursor_get_hotspot(int32_t* x, int32_t* y);
int compositor_cursor_set_custom_data(void* data);
void* compositor_cursor_get_custom_data(void);
int compositor_cursor_set_animated(bool animated);
bool compositor_cursor_is_animated(void);
int compositor_cursor_set_size(int32_t width, int32_t height);
void compositor_cursor_get_size(int32_t* width, int32_t* height);
int compositor_cursor_set_pixels(const uint32_t* pixels);
const uint32_t* compositor_cursor_get_pixels(void);
int compositor_cursor_update(float delta_time);
int compositor_cursor_load_theme(const char* theme_name);
int compositor_cursor_set_config(bool auto_hide, int32_t hide_timeout, 
                                bool acceleration, float sensitivity, 
                                bool theme_enabled, const char* theme_name, 
                                int32_t size);
void compositor_cursor_get_config(bool* auto_hide, int32_t* hide_timeout, 
                                 bool* acceleration, float* sensitivity, 
                                 bool* theme_enabled, const char** theme_name, 
                                 int32_t* size);

#endif // COMPOSITOR_INPUT_CURSOR_H