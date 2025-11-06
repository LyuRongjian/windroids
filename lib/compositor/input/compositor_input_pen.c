/*
 * WinDroids Compositor
 * Pen Input Module
 */

#include "compositor_input_pen.h"
#include "compositor_input.h"
#include "compositor_utils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// 最大支持的触控笔设备数量
#define MAX_PEN_DEVICES 8

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 触控笔配置
static CompositorPenConfig g_pen_config = {
    .enable_pressure = true,
    .enable_tilt = true,
    .enable_rotation = true,
    .enable_distance = true,
    .enable_multi_tool = true,
    .pressure_sensitivity = 1.0f,
    .tilt_sensitivity = 1.0f,
    .rotation_sensitivity = 1.0f,
    .distance_threshold = 0.1f,
    .pressure_threshold = 0.01f,
    .map_to_mouse = true,
    .tip_button_map = 0,      // 笔尖按钮映射到鼠标左键
    .lower_button_map = 2,    // 下部按钮映射到鼠标中键
    .upper_button_map = 1,    // 上部按钮映射到鼠标右键
    .barrel_button_map = 3    // 侧边按钮映射到鼠标后退键
};

// 触控笔设备状态数组
static CompositorPenState g_pen_states[MAX_PEN_DEVICES];
static CompositorPenDeviceInfo g_pen_device_infos[MAX_PEN_DEVICES];
static bool g_pen_devices_initialized[MAX_PEN_DEVICES] = {false};

// 事件回调
static CompositorPenEventCallback g_pen_callback = NULL;
static void* g_pen_callback_user_data = NULL;

// 内部辅助函数声明
static int get_pen_state_index(int device_id);
static void send_pen_event(int device_id);
static void map_pen_to_mouse_events(int device_id);
static float apply_pressure_sensitivity(float pressure);
static float apply_tilt_sensitivity(float tilt);
static float apply_rotation_sensitivity(float rotation);

// 设置合成器状态引用
void compositor_input_pen_set_state(CompositorState* state)
{
    g_compositor_state = state;
}

// 初始化触控笔模块
int compositor_input_pen_init(void)
{
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 初始化触控笔设备状态
    memset(g_pen_states, 0, sizeof(g_pen_states));
    memset(g_pen_device_infos, 0, sizeof(g_pen_device_infos));
    memset(g_pen_devices_initialized, 0, sizeof(g_pen_devices_initialized));
    
    // 设置默认配置
    g_pen_config.enable_pressure = true;
    g_pen_config.enable_tilt = true;
    g_pen_config.enable_rotation = true;
    g_pen_config.enable_distance = true;
    g_pen_config.enable_multi_tool = true;
    g_pen_config.pressure_sensitivity = 1.0f;
    g_pen_config.tilt_sensitivity = 1.0f;
    g_pen_config.rotation_sensitivity = 1.0f;
    g_pen_config.distance_threshold = 0.1f;
    g_pen_config.pressure_threshold = 0.01f;
    g_pen_config.map_to_mouse = true;
    g_pen_config.tip_button_map = 0;      // 笔尖按钮映射到鼠标左键
    g_pen_config.lower_button_map = 2;    // 下部按钮映射到鼠标中键
    g_pen_config.upper_button_map = 1;    // 上部按钮映射到鼠标右键
    g_pen_config.barrel_button_map = 3;   // 侧边按钮映射到鼠标后退键
    
    return COMPOSITOR_OK;
}

// 清理触控笔模块
void compositor_input_pen_cleanup(void)
{
    // 清理触控笔相关资源
    memset(g_pen_states, 0, sizeof(g_pen_states));
    memset(g_pen_device_infos, 0, sizeof(g_pen_device_infos));
    memset(g_pen_devices_initialized, 0, sizeof(g_pen_devices_initialized));
    
    g_pen_callback = NULL;
    g_pen_callback_user_data = NULL;
}

// 获取触控笔状态索引
static int get_pen_state_index(int device_id)
{
    for (int i = 0; i < MAX_PEN_DEVICES; i++) {
        if (g_pen_devices_initialized[i] && g_pen_device_infos[i].device_id == device_id) {
            return i;
        }
    }
    
    // 如果没找到，尝试分配一个新的槽位
    for (int i = 0; i < MAX_PEN_DEVICES; i++) {
        if (!g_pen_devices_initialized[i]) {
            g_pen_device_infos[i].device_id = device_id;
            g_pen_devices_initialized[i] = true;
            
            // 设置默认设备信息
            strcpy(g_pen_device_infos[i].name, "Unknown Pen Device");
            g_pen_device_infos[i].has_pressure = true;
            g_pen_device_infos[i].has_tilt = true;
            g_pen_device_infos[i].has_rotation = true;
            g_pen_device_infos[i].has_distance = true;
            g_pen_device_infos[i].has_multi_tool = false;
            g_pen_device_infos[i].max_pressure = 1.0f;
            g_pen_device_infos[i].max_tilt = 90.0f;
            g_pen_device_infos[i].max_rotation = 360.0f;
            g_pen_device_infos[i].max_distance = 1.0f;
            g_pen_device_infos[i].num_buttons = 2;
            g_pen_device_infos[i].supported_tools[0] = COMPOSITOR_PEN_TOOL_TYPE_PEN;
            g_pen_device_infos[i].supported_tools[1] = COMPOSITOR_PEN_TOOL_TYPE_ERASER;
            g_pen_device_infos[i].num_supported_tools = 2;
            
            // 初始化状态
            memset(&g_pen_states[i], 0, sizeof(g_pen_states[i]));
            g_pen_states[i].tool_type = COMPOSITOR_PEN_TOOL_TYPE_PEN;
            g_pen_states[i].timestamp = compositor_get_time();
            
            return i;
        }
    }
    
    return -1; // 没有可用的槽位
}

// 发送触控笔事件
static void send_pen_event(int device_id)
{
    if (g_pen_callback) {
        int index = get_pen_state_index(device_id);
        if (index >= 0) {
            g_pen_callback(&g_pen_states[index], g_pen_callback_user_data);
        }
    }
}

// 将触控笔事件映射到鼠标事件
static void map_pen_to_mouse_events(int device_id)
{
    if (!g_pen_config.map_to_mouse) {
        return;
    }
    
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 发送鼠标移动事件
    compositor_handle_input(COMPOSITOR_INPUT_MOUSE_MOTION, state->x, state->y, 0, 0);
    
    // 根据压力值发送鼠标按钮事件
    bool tip_pressed = state->in_contact && state->pressure > g_pen_config.pressure_threshold;
    if (tip_pressed != state->buttons[COMPOSITOR_PEN_BUTTON_TIP]) {
        state->buttons[COMPOSITOR_PEN_BUTTON_TIP] = tip_pressed;
        compositor_handle_input(COMPOSITOR_INPUT_MOUSE_BUTTON, state->x, state->y, 
                               g_pen_config.tip_button_map, tip_pressed ? 1 : 0);
    }
    
    // 处理其他按钮
    if (state->buttons[COMPOSITOR_PEN_BUTTON_LOWER]) {
        compositor_handle_input(COMPOSITOR_INPUT_MOUSE_BUTTON, state->x, state->y, 
                               g_pen_config.lower_button_map, 1);
    }
    
    if (state->buttons[COMPOSITOR_PEN_BUTTON_UPPER]) {
        compositor_handle_input(COMPOSITOR_INPUT_MOUSE_BUTTON, state->x, state->y, 
                               g_pen_config.upper_button_map, 1);
    }
    
    if (state->buttons[COMPOSITOR_PEN_BUTTON_BARREL]) {
        compositor_handle_input(COMPOSITOR_INPUT_MOUSE_BUTTON, state->x, state->y, 
                               g_pen_config.barrel_button_map, 1);
    }
}

// 应用压力灵敏度
static float apply_pressure_sensitivity(float pressure)
{
    if (!g_pen_config.enable_pressure) {
        return 0.0f;
    }
    
    // 应用灵敏度曲线
    float adjusted = pressure * g_pen_config.pressure_sensitivity;
    
    // 限制在0.0-1.0范围内
    if (adjusted < 0.0f) adjusted = 0.0f;
    if (adjusted > 1.0f) adjusted = 1.0f;
    
    return adjusted;
}

// 应用倾斜灵敏度
static float apply_tilt_sensitivity(float tilt)
{
    if (!g_pen_config.enable_tilt) {
        return 0.0f;
    }
    
    // 应用灵敏度
    float adjusted = tilt * g_pen_config.tilt_sensitivity;
    
    // 限制在-90.0到90.0范围内
    if (adjusted < -90.0f) adjusted = -90.0f;
    if (adjusted > 90.0f) adjusted = 90.0f;
    
    return adjusted;
}

// 应用旋转灵敏度
static float apply_rotation_sensitivity(float rotation)
{
    if (!g_pen_config.enable_rotation) {
        return 0.0f;
    }
    
    // 应用灵敏度
    float adjusted = rotation * g_pen_config.rotation_sensitivity;
    
    // 限制在0.0到360.0范围内
    if (adjusted < 0.0f) adjusted += 360.0f;
    if (adjusted >= 360.0f) adjusted -= 360.0f;
    
    return adjusted;
}

// 处理触控笔移动事件
void compositor_input_handle_pen_motion(int device_id, float x, float y)
{
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 更新位置
    state->x = x;
    state->y = y;
    state->timestamp = compositor_get_time();
    
    // 发送事件
    send_pen_event(device_id);
    map_pen_to_mouse_events(device_id);
}

// 处理触控笔压力事件
void compositor_input_handle_pen_pressure(int device_id, float pressure)
{
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 应用压力灵敏度
    state->pressure = apply_pressure_sensitivity(pressure);
    state->timestamp = compositor_get_time();
    
    // 发送事件
    send_pen_event(device_id);
    map_pen_to_mouse_events(device_id);
}

// 处理触控笔倾斜事件
void compositor_input_handle_pen_tilt(int device_id, float tilt_x, float tilt_y)
{
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 应用倾斜灵敏度
    state->tilt_x = apply_tilt_sensitivity(tilt_x);
    state->tilt_y = apply_tilt_sensitivity(tilt_y);
    state->timestamp = compositor_get_time();
    
    // 发送事件
    send_pen_event(device_id);
}

// 处理触控笔旋转事件
void compositor_input_handle_pen_rotation(int device_id, float rotation)
{
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 应用旋转灵敏度
    state->rotation = apply_rotation_sensitivity(rotation);
    state->timestamp = compositor_get_time();
    
    // 发送事件
    send_pen_event(device_id);
}

// 处理触控笔距离事件
void compositor_input_handle_pen_distance(int device_id, float distance)
{
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 更新距离值
    state->distance = distance;
    state->timestamp = compositor_get_time();
    
    // 如果距离小于阈值，则认为在范围内
    state->in_range = (distance < g_pen_config.distance_threshold);
    
    // 发送事件
    send_pen_event(device_id);
}

// 处理触控笔按钮事件
void compositor_input_handle_pen_button(int device_id, CompositorPenButton button, bool pressed)
{
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 更新按钮状态
    if (button >= 0 && button < COMPOSITOR_PEN_BUTTON_MAX) {
        state->buttons[button] = pressed;
        state->timestamp = compositor_get_time();
    }
    
    // 发送事件
    send_pen_event(device_id);
    map_pen_to_mouse_events(device_id);
}

// 处理触控笔接近事件
void compositor_input_handle_pen_proximity(int device_id, bool in_range)
{
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 更新接近状态
    state->in_range = in_range;
    state->timestamp = compositor_get_time();
    
    // 如果不在范围内，则认为不接触
    if (!in_range) {
        state->in_contact = false;
    }
    
    // 发送事件
    send_pen_event(device_id);
}

// 处理触控笔接触事件
void compositor_input_handle_pen_contact(int device_id, bool in_contact)
{
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 更新接触状态
    state->in_contact = in_contact;
    state->timestamp = compositor_get_time();
    
    // 如果接触，则认为在范围内
    if (in_contact) {
        state->in_range = true;
    }
    
    // 发送事件
    send_pen_event(device_id);
    map_pen_to_mouse_events(device_id);
}

// 处理触控笔工具类型变化事件
void compositor_input_handle_pen_tool_change(int device_id, CompositorPenToolType tool_type)
{
    int index = get_pen_state_index(device_id);
    if (index < 0) {
        return;
    }
    
    CompositorPenState* state = &g_pen_states[index];
    
    // 更新工具类型
    state->tool_type = tool_type;
    state->timestamp = compositor_get_time();
    
    // 发送事件
    send_pen_event(device_id);
}

// 设置触控笔配置
void compositor_input_set_pen_config(const CompositorPenConfig* config)
{
    if (config) {
        memcpy(&g_pen_config, config, sizeof(g_pen_config));
    }
}

// 获取触控笔配置
CompositorPenConfig* compositor_input_get_pen_config(void)
{
    return &g_pen_config;
}

// 获取触控笔状态
CompositorPenState* compositor_input_get_pen_state(int device_id)
{
    int index = get_pen_state_index(device_id);
    if (index >= 0) {
        return &g_pen_states[index];
    }
    return NULL;
}

// 获取触控笔设备信息
CompositorPenDeviceInfo* compositor_input_get_pen_device_info(int device_id)
{
    int index = get_pen_state_index(device_id);
    if (index >= 0) {
        return &g_pen_device_infos[index];
    }
    return NULL;
}

// 注册触控笔事件回调
void compositor_input_register_pen_callback(CompositorPenEventCallback callback, void* user_data)
{
    g_pen_callback = callback;
    g_pen_callback_user_data = user_data;
}

// 取消注册触控笔事件回调
void compositor_input_unregister_pen_callback(CompositorPenEventCallback callback)
{
    if (g_pen_callback == callback) {
        g_pen_callback = NULL;
        g_pen_callback_user_data = NULL;
    }
}

// 更新触控笔状态（由主循环调用）
void compositor_input_pen_update(void)
{
    // 这里可以添加定期更新逻辑，例如处理超时等
}

// 设备能力查询函数
bool compositor_input_pen_has_pressure_support(int device_id)
{
    CompositorPenDeviceInfo* info = compositor_input_get_pen_device_info(device_id);
    return info ? info->has_pressure : false;
}

bool compositor_input_pen_has_tilt_support(int device_id)
{
    CompositorPenDeviceInfo* info = compositor_input_get_pen_device_info(device_id);
    return info ? info->has_tilt : false;
}

bool compositor_input_pen_has_rotation_support(int device_id)
{
    CompositorPenDeviceInfo* info = compositor_input_get_pen_device_info(device_id);
    return info ? info->has_rotation : false;
}

bool compositor_input_pen_has_distance_support(int device_id)
{
    CompositorPenDeviceInfo* info = compositor_input_get_pen_device_info(device_id);
    return info ? info->has_distance : false;
}

bool compositor_input_pen_has_multi_tool_support(int device_id)
{
    CompositorPenDeviceInfo* info = compositor_input_get_pen_device_info(device_id);
    return info ? info->has_multi_tool : false;
}