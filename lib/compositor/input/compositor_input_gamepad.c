/*
 * WinDroids Compositor
 * Gamepad and Pen Input Module
 */

#include "compositor_input_gamepad.h"
#include "compositor_input.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 游戏手柄配置
static CompositorGamepadConfig g_gamepad_config = {
    .enable_mouse_emulation = true,
    .sensitivity = 1.0f,
    .deadzone = 0.15f,
    .max_speed = 10
};

// 触控笔配置
static CompositorPenConfig g_pen_config = {
    .enable_pressure = true,
    .enable_tilt = true,
    .pressure_sensitivity = 1.0f
};

// 设备能力标志
static bool g_device_capabilities[8] = {
    true,  // 键盘
    true,  // 鼠标
    true,  // 触摸屏
    false, // 触控笔
    true,  // 游戏手柄
    true,  // 摇杆
    true,  // 触摸板
    true   // 轨迹球
};

// 设置合成器状态引用
void compositor_input_gamepad_set_state(CompositorState* state)
{
    g_compositor_state = state;
}

// 初始化游戏手柄/触控笔模块
int compositor_input_gamepad_init(void)
{
    if (!g_compositor_state) {
        return -1;
    }
    
    // 初始化游戏手柄和触控笔相关资源
    memset(&g_gamepad_config, 0, sizeof(g_gamepad_config));
    memset(&g_pen_config, 0, sizeof(g_pen_config));
    
    // 设置默认配置
    g_gamepad_config.enable_mouse_emulation = true;
    g_gamepad_config.sensitivity = 1.0f;
    g_gamepad_config.deadzone = 0.15f;
    g_gamepad_config.max_speed = 10;
    
    g_pen_config.enable_pressure = true;
    g_pen_config.enable_tilt = true;
    g_pen_config.pressure_sensitivity = 1.0f;
    
    return 0;
}

// 清理游戏手柄/触控笔模块
void compositor_input_gamepad_cleanup(void)
{
    // 清理游戏手柄和触控笔相关资源
    memset(&g_gamepad_config, 0, sizeof(g_gamepad_config));
    memset(&g_pen_config, 0, sizeof(g_pen_config));
}

// 处理游戏手柄按钮事件
void compositor_input_handle_gamepad_button(int device_id, int button, int state)
{
    if (!g_compositor_state) {
        return;
    }
    
    // 获取设备
    CompositorInputDevice* device = compositor_input_get_device(device_id);
    if (!device || device->type != COMPOSITOR_DEVICE_TYPE_GAMEPAD) {
        return;
    }
    
    // 更新按钮状态
    CompositorGamepadState* gamepad_state = &device->gamepad_buttons;
    
    switch (button) {
        case 0: gamepad_state->a = (state == 1); break;
        case 1: gamepad_state->b = (state == 1); break;
        case 2: gamepad_state->x = (state == 1); break;
        case 3: gamepad_state->y = (state == 1); break;
        case 4: gamepad_state->dpad_up = (state == 1); break;
        case 5: gamepad_state->dpad_down = (state == 1); break;
        case 6: gamepad_state->dpad_left = (state == 1); break;
        case 7: gamepad_state->dpad_right = (state == 1); break;
        case 8: gamepad_state->l1 = (state == 1); break;
        case 9: gamepad_state->r1 = (state == 1); break;
        case 10: gamepad_state->l2 = (state == 1); break;
        case 11: gamepad_state->r2 = (state == 1); break;
        case 12: gamepad_state->select = (state == 1); break;
        case 13: gamepad_state->start = (state == 1); break;
        case 14: gamepad_state->home = (state == 1); break;
        case 15: gamepad_state->l3 = (state == 1); break;
        case 16: gamepad_state->r3 = (state == 1); break;
    }
    
    // 如果启用了鼠标模拟，将按钮映射到鼠标事件
    if (g_gamepad_config.enable_mouse_emulation) {
        // 映射常见按钮到鼠标按键
        if (button == 0) {  // A按钮映射为鼠标左键
            compositor_handle_input(COMPOSITOR_INPUT_MOUSE_BUTTON, 0, 0, COMPOSITOR_MOUSE_BUTTON_LEFT, state);
        } else if (button == 1) {  // B按钮映射为鼠标右键
            compositor_handle_input(COMPOSITOR_INPUT_MOUSE_BUTTON, 0, 0, COMPOSITOR_MOUSE_BUTTON_RIGHT, state);
        } else if (button == 2) {  // X按钮映射为鼠标中键
            compositor_handle_input(COMPOSITOR_INPUT_MOUSE_BUTTON, 0, 0, COMPOSITOR_MOUSE_BUTTON_MIDDLE, state);
        }
    }
}

// 处理游戏手柄摇杆事件
void compositor_input_handle_gamepad_joystick(int device_id, int joystick, float x, float y)
{
    if (!g_compositor_state) {
        return;
    }
    
    // 获取设备
    CompositorInputDevice* device = compositor_input_get_device(device_id);
    if (!device || device->type != COMPOSITOR_DEVICE_TYPE_GAMEPAD) {
        return;
    }
    
    // 更新摇杆状态
    CompositorGamepadState* gamepad_state = &device->gamepad_buttons;
    
    if (joystick == 0) {  // 左摇杆
        gamepad_state->lx = x;
        gamepad_state->ly = y;
    } else if (joystick == 1) {  // 右摇杆
        gamepad_state->rx = x;
        gamepad_state->ry = y;
    } else {
        return;
    }
    
    // 如果启用了鼠标模拟，将摇杆映射到鼠标移动
    if (g_gamepad_config.enable_mouse_emulation && joystick == 0) {
        // 应用死区
        float magnitude = sqrtf(x * x + y * y);
        if (magnitude < g_gamepad_config.deadzone) {
            return;
        }
        
        // 应用灵敏度
        float dx = x * g_gamepad_config.sensitivity;
        float dy = y * g_gamepad_config.sensitivity;
        
        // 限制最大速度
        if (fabsf(dx) > g_gamepad_config.max_speed) {
            dx = (dx > 0) ? g_gamepad_config.max_speed : -g_gamepad_config.max_speed;
        }
        if (fabsf(dy) > g_gamepad_config.max_speed) {
            dy = (dy > 0) ? g_gamepad_config.max_speed : -g_gamepad_config.max_speed;
        }
        
        // 发送鼠标移动事件
        compositor_handle_input(COMPOSITOR_INPUT_MOUSE_MOTION, dx, dy, 0, 0);
    }
}

// 处理游戏手柄扳机事件
void compositor_input_handle_gamepad_trigger(int device_id, int trigger, float value)
{
    if (!g_compositor_state) {
        return;
    }
    
    // 获取设备
    CompositorInputDevice* device = compositor_input_get_device(device_id);
    if (!device || device->type != COMPOSITOR_DEVICE_TYPE_GAMEPAD) {
        return;
    }
    
    // 更新扳机状态
    CompositorGamepadState* gamepad_state = &device->gamepad_buttons;
    
    if (trigger == 0) {  // L2扳机
        gamepad_state->l2_value = value;
    } else if (trigger == 1) {  // R2扳机
        gamepad_state->r2_value = value;
    } else {
        return;
    }
    
    // 如果启用了鼠标模拟，将扳机映射到滚动事件
    if (g_gamepad_config.enable_mouse_emulation) {
        if (trigger == 0) {  // L2扳机映射为向上滚动
            compositor_handle_scroll(0, -value * 5.0f, device_id);
        } else if (trigger == 1) {  // R2扳机映射为向下滚动
            compositor_handle_scroll(0, value * 5.0f, device_id);
        }
    }
}

// 设置游戏手柄配置
void compositor_input_set_gamepad_config(bool enable_mouse_emulation, float sensitivity, 
                                        float deadzone, int max_speed)
{
    g_gamepad_config.enable_mouse_emulation = enable_mouse_emulation;
    g_gamepad_config.sensitivity = sensitivity;
    g_gamepad_config.deadzone = deadzone;
    g_gamepad_config.max_speed = max_speed;
}

// 设置触控笔配置
void compositor_input_set_pen_config(bool enable_pressure, bool enable_tilt, float pressure_sensitivity)
{
    g_pen_config.enable_pressure = enable_pressure;
    g_pen_config.enable_tilt = enable_tilt;
    g_pen_config.pressure_sensitivity = pressure_sensitivity;
}

// 获取游戏手柄配置
CompositorGamepadConfig* compositor_input_get_gamepad_config(void)
{
    return &g_gamepad_config;
}

// 获取触控笔配置
CompositorPenConfig* compositor_input_get_pen_config(void)
{
    return &g_pen_config;
}

// 设备能力查询函数
bool compositor_input_is_device_type_supported(int32_t device_type)
{
    if (device_type >= 0 && device_type < 8) {
        return g_device_capabilities[device_type];
    }
    return false;
}

bool compositor_input_has_pressure_support(void)
{
    return g_pen_config.enable_pressure;
}

bool compositor_input_has_tilt_support(void)
{
    return g_pen_config.enable_tilt;
}

bool compositor_input_has_rotation_support(void)
{
    return true;  // 默认支持旋转
}