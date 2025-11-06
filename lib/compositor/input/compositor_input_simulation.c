#include "compositor_input_simulation.h"
#include "compositor_utils.h"
#include <string.h>
#include <stdlib.h>

// 输入模拟初始化
int compositor_input_simulation_init(void) {
    return COMPOSITOR_OK;
}

// 输入模拟清理
void compositor_input_simulation_cleanup(void) {
    // 无需特殊清理
}

// 模拟输入事件
int compositor_input_simulate_event(CompositorInputEventType type, int x, int y, int state) {
    if (!g_compositor_state) {
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    CompositorInputEvent event;
    memset(&event, 0, sizeof(CompositorInputEvent));
    event.type = type;
    event.x = x;
    event.y = y;
    event.state = state;
    event.device_id = -1;  // 模拟事件
    
    return compositor_handle_input_event(&event);
}

// 模拟鼠标按钮事件
void simulate_mouse_button(int x, int y, int button, int state) {
    compositor_input_dispatcher_simulate_mouse_event(x, y, button, state == COMPOSITOR_INPUT_STATE_PRESSED);
}

// 模拟鼠标移动事件
void simulate_mouse_motion(int x, int y) {
    if (!g_compositor_state) {
        log_message(COMPOSITOR_LOG_ERROR, "Compositor not initialized, cannot simulate mouse motion");
        return;
    }
    
    // 更新鼠标位置
    g_compositor_state->mouse_x = x;
    g_compositor_state->mouse_y = y;
    
    CompositorInputEvent event;
    memset(&event, 0, sizeof(CompositorInputEvent));
    event.type = COMPOSITOR_INPUT_EVENT_MOUSE_MOTION;
    event.x = x;
    event.y = y;
    event.state = COMPOSITOR_INPUT_STATE_MOTION;
    event.device_id = -1;  // 模拟事件
    
    compositor_handle_input_event(&event);
}

// 模拟键盘按键事件
void simulate_keyboard_key(int keycode, int state) {
    compositor_input_dispatcher_simulate_keyboard_event(keycode, state == COMPOSITOR_INPUT_STATE_PRESSED);
}