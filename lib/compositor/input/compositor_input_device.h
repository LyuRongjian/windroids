/*
 * WinDroids Compositor
 * Input Device Management Module
 */

#ifndef COMPOSITOR_INPUT_DEVICE_H
#define COMPOSITOR_INPUT_DEVICE_H

#include "compositor.h"
#include <stdbool.h>

// 输入设备类型枚举
typedef enum {
    COMPOSITOR_DEVICE_TYPE_KEYBOARD,
    COMPOSITOR_DEVICE_TYPE_MOUSE,
    COMPOSITOR_DEVICE_TYPE_TOUCHSCREEN,
    COMPOSITOR_DEVICE_TYPE_PEN,
    COMPOSITOR_DEVICE_TYPE_GAMEPAD,
    COMPOSITOR_DEVICE_TYPE_JOYSTICK,
    COMPOSITOR_DEVICE_TYPE_TRACKPAD,
    COMPOSITOR_DEVICE_TYPE_TRACKBALL,
    COMPOSITOR_DEVICE_TYPE_UNKNOWN
} CompositorInputDeviceType;

// 游戏控制器按钮状态结构体
typedef struct {
    bool a, b, x, y;
    bool dpad_up, dpad_down, dpad_left, dpad_right;
    bool l1, r1, l2, r2;
    bool select, start, home;
    bool l3, r3;
    float lx, ly, rx, ry; // 摇杆位置 (-1.0 到 1.0)
    float l2_value, r2_value; // 扳机值 (0.0 到 1.0)
} CompositorGamepadState;

// 输入设备结构体
typedef struct {
    int device_id;
    CompositorInputDeviceType type;
    char* name;
    bool enabled;
    void* device_data; // 设备特定数据
    
    // 设备能力标志
    bool has_pressure_sensor;
    bool has_tilt_sensor;
    bool has_rotation_sensor;
    bool has_accelerometer;
    
    // 游戏控制器状态
    CompositorGamepadState gamepad_buttons;
} CompositorInputDevice;

// 输入捕获模式枚举
typedef enum {
    COMPOSITOR_INPUT_CAPTURE_MODE_NORMAL,
    COMPOSITOR_INPUT_CAPTURE_MODE_GRAB,
    COMPOSITOR_INPUT_CAPTURE_MODE_EXCLUSIVE,
    COMPOSITOR_INPUT_CAPTURE_MODE_DISABLED
} CompositorInputCaptureMode;

// 设备管理函数声明

// 设置合成器状态引用（供内部使用）
void compositor_input_device_set_state(CompositorState* state);

// 初始化设备管理系统
int compositor_input_device_init(void);

// 清理设备管理系统
void compositor_input_device_cleanup(void);

// 注册输入设备
int compositor_input_register_device(CompositorInputDeviceType type, const char* name, int device_id);

// 注销输入设备
int compositor_input_unregister_device(int device_id);

// 启用/禁用输入设备
int compositor_input_enable_device(int device_id, bool enabled);

// 获取设备状态
CompositorInputDevice* compositor_input_get_device(int device_id);

// 获取设备数量
int compositor_input_get_device_count(void);

// 获取设备列表
CompositorInputDevice* compositor_input_get_devices(void);

// 设置设备特定配置
int compositor_input_set_device_config(int device_id, void* config);

// 获取设备特定配置
void* compositor_input_get_device_config(int device_id);

// 获取活动输入设备
CompositorInputDevice* compositor_input_get_active_device(void);

// 设置活动输入设备
void compositor_input_set_active_device(int device_id);

#endif // COMPOSITOR_INPUT_DEVICE_H