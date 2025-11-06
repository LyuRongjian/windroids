/*
 * WinDroids Compositor
 * Pen Input Module
 */

#ifndef COMPOSITOR_INPUT_PEN_H
#define COMPOSITOR_INPUT_PEN_H

#include "compositor.h"
#include "compositor_input_device.h"
#include "compositor_input_type.h"
#include "compositor_utils.h"
#include <stdbool.h>

// 触控笔按钮枚举
typedef enum {
    COMPOSITOR_PEN_BUTTON_TIP = 0,      // 笔尖按钮
    COMPOSITOR_PEN_BUTTON_LOWER = 1,    // 下部按钮
    COMPOSITOR_PEN_BUTTON_UPPER = 2,    // 上部按钮
    COMPOSITOR_PEN_BUTTON_BARREL = 3,   // 侧边按钮
    COMPOSITOR_PEN_BUTTON_MAX = 4
} CompositorPenButton;

// 触控笔工具类型枚举
typedef enum {
    COMPOSITOR_PEN_TOOL_TYPE_UNKNOWN = 0,
    COMPOSITOR_PEN_TOOL_TYPE_PEN = 1,       // 标准触控笔
    COMPOSITOR_PEN_TOOL_TYPE_ERASER = 2,    // 橡皮擦
    COMPOSITOR_PEN_TOOL_TYPE_BRUSH = 3,     // 画笔
    COMPOSITOR_PEN_TOOL_TYPE_PENCIL = 4,    // 铅笔
    COMPOSITOR_PEN_TOOL_TYPE_AIRBRUSH = 5,  // 喷枪
    COMPOSITOR_PEN_TOOL_TYPE_MARKER = 6     // 马克笔
} CompositorPenToolType;

// 触控笔状态结构体
typedef struct {
    float x;                        // X坐标
    float y;                        // Y坐标
    float pressure;                 // 压力值 (0.0-1.0)
    float tilt_x;                   // X轴倾斜角度 (-90.0 到 90.0)
    float tilt_y;                   // Y轴倾斜角度 (-90.0 到 90.0)
    float rotation;                 // 旋转角度 (0.0-360.0)
    float distance;                 // 距离值 (0.0-1.0)
    bool buttons[COMPOSITOR_PEN_BUTTON_MAX]; // 按钮状态
    bool in_range;                  // 是否在范围内
    bool in_contact;                // 是否接触表面
    CompositorPenToolType tool_type; // 工具类型
    uint64_t timestamp;             // 时间戳
} CompositorPenState;

// 触控笔配置结构体
typedef struct {
    bool enable_pressure;           // 启用压力感应
    bool enable_tilt;               // 启用倾斜支持
    bool enable_rotation;           // 启用旋转支持
    bool enable_distance;           // 启用距离支持
    bool enable_multi_tool;         // 启用多工具支持
    float pressure_sensitivity;     // 压力灵敏度
    float tilt_sensitivity;         // 倾斜灵敏度
    float rotation_sensitivity;     // 旋转灵敏度
    float distance_threshold;       // 距离阈值
    float pressure_threshold;       // 压力阈值
    bool map_to_mouse;             // 映射到鼠标事件
    int tip_button_map;            // 笔尖按钮映射到鼠标按钮
    int lower_button_map;          // 下部按钮映射到鼠标按钮
    int upper_button_map;          // 上部按钮映射到鼠标按钮
    int barrel_button_map;         // 侧边按钮映射到鼠标按钮
} CompositorPenConfig;

// 触控笔设备信息结构体
typedef struct {
    int32_t device_id;              // 设备ID
    char name[256];                 // 设备名称
    char vendor[128];               // 厂商
    char product[128];              // 产品
    bool has_pressure;              // 是否支持压力
    bool has_tilt;                  // 是否支持倾斜
    bool has_rotation;              // 是否支持旋转
    bool has_distance;              // 是否支持距离
    bool has_multi_tool;            // 是否支持多工具
    float max_pressure;             // 最大压力值
    float max_tilt;                 // 最大倾斜角度
    float max_rotation;             // 最大旋转角度
    float max_distance;             // 最大距离值
    int32_t num_buttons;            // 按钮数量
    CompositorPenToolType supported_tools[8]; // 支持的工具类型
    int32_t num_supported_tools;    // 支持的工具类型数量
} CompositorPenDeviceInfo;

// 触控笔事件回调函数类型
typedef void (*CompositorPenEventCallback)(const CompositorPenState* state, void* user_data);

// 触控笔模块函数声明

// 设置合成器状态引用（供内部使用）
void compositor_input_pen_set_state(CompositorState* state);

// 初始化触控笔模块
int compositor_input_pen_init(void);

// 清理触控笔模块
void compositor_input_pen_cleanup(void);

// 处理触控笔移动事件
void compositor_input_handle_pen_motion(int device_id, float x, float y);

// 处理触控笔压力事件
void compositor_input_handle_pen_pressure(int device_id, float pressure);

// 处理触控笔倾斜事件
void compositor_input_handle_pen_tilt(int device_id, float tilt_x, float tilt_y);

// 处理触控笔旋转事件
void compositor_input_handle_pen_rotation(int device_id, float rotation);

// 处理触控笔距离事件
void compositor_input_handle_pen_distance(int device_id, float distance);

// 处理触控笔按钮事件
void compositor_input_handle_pen_button(int device_id, CompositorPenButton button, bool pressed);

// 处理触控笔接近事件
void compositor_input_handle_pen_proximity(int device_id, bool in_range);

// 处理触控笔接触事件
void compositor_input_handle_pen_contact(int device_id, bool in_contact);

// 处理触控笔工具类型变化事件
void compositor_input_handle_pen_tool_change(int device_id, CompositorPenToolType tool_type);

// 设置触控笔配置
void compositor_input_set_pen_config(const CompositorPenConfig* config);

// 获取触控笔配置
CompositorPenConfig* compositor_input_get_pen_config(void);

// 获取触控笔状态
CompositorPenState* compositor_input_get_pen_state(int device_id);

// 获取触控笔设备信息
CompositorPenDeviceInfo* compositor_input_get_pen_device_info(int device_id);

// 注册触控笔事件回调
void compositor_input_register_pen_callback(CompositorPenEventCallback callback, void* user_data);

// 取消注册触控笔事件回调
void compositor_input_unregister_pen_callback(CompositorPenEventCallback callback);

// 更新触控笔状态（由主循环调用）
void compositor_input_pen_update(void);

// 设备能力查询函数
bool compositor_input_pen_has_pressure_support(int device_id);
bool compositor_input_pen_has_tilt_support(int device_id);
bool compositor_input_pen_has_rotation_support(int device_id);
bool compositor_input_pen_has_distance_support(int device_id);
bool compositor_input_pen_has_multi_tool_support(int device_id);

#endif // COMPOSITOR_INPUT_PEN_H