/*
 * WinDroids Compositor
 * Gamepad and Pen Input Module
 */

#ifndef COMPOSITOR_INPUT_GAMEPAD_H
#define COMPOSITOR_INPUT_GAMEPAD_H

#include "compositor.h"
#include "compositor_input_device.h"
#include "compositor_utils.h"
#include <stdbool.h>

// 游戏手柄配置结构体
typedef struct {
    bool enable_mouse_emulation;  // 启用鼠标模拟
    float sensitivity;            // 摇杆灵敏度
    float deadzone;               // 摇杆死区
    int max_speed;               // 摇杆最大速度
} CompositorGamepadConfig;

// 触控笔配置结构体
typedef struct {
    bool enable_pressure;         // 启用压力感应
    bool enable_tilt;             // 启用倾斜支持
    float pressure_sensitivity;   // 压力灵敏度
} CompositorPenConfig;

// 游戏手柄模块函数声明

// 设置合成器状态引用（供内部使用）
void compositor_input_gamepad_set_state(CompositorState* state);

// 初始化游戏手柄/触控笔模块
int compositor_input_gamepad_init(void);

// 清理游戏手柄/触控笔模块
void compositor_input_gamepad_cleanup(void);

// 处理游戏手柄按钮事件
void compositor_input_handle_gamepad_button(int device_id, int button, int state);

// 处理游戏手柄摇杆事件
void compositor_input_handle_gamepad_joystick(int device_id, int joystick, float x, float y);

// 处理游戏手柄扳机事件
void compositor_input_handle_gamepad_trigger(int device_id, int trigger, float value);

// 设置游戏手柄配置
void compositor_input_set_gamepad_config(bool enable_mouse_emulation, float sensitivity, 
                                        float deadzone, int max_speed);

// 设置触控笔配置
void compositor_input_set_pen_config(bool enable_pressure, bool enable_tilt, float pressure_sensitivity);

// 获取游戏手柄配置
CompositorGamepadConfig* compositor_input_get_gamepad_config(void);

// 获取触控笔配置
CompositorPenConfig* compositor_input_get_pen_config(void);

// 设备能力查询函数
bool compositor_input_is_device_type_supported(int32_t device_type);
bool compositor_input_has_pressure_support(void);
bool compositor_input_has_tilt_support(void);
bool compositor_input_has_rotation_support(void);

#endif // COMPOSITOR_INPUT_GAMEPAD_H