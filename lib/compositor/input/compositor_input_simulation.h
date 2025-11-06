#ifndef COMPOSITOR_INPUT_SIMULATION_H
#define COMPOSITOR_INPUT_SIMULATION_H

#include "compositor_input_types.h"
#include "compositor_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

// 输入模拟初始化和清理
int compositor_input_simulation_init(void);
void compositor_input_simulation_cleanup(void);

// 输入事件模拟
int compositor_input_simulate_event(CompositorInputEventType type, int x, int y, int state);
void simulate_mouse_button(int x, int y, int button, int state);
void simulate_mouse_motion(int x, int y);
void simulate_keyboard_key(int keycode, int state);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_INPUT_SIMULATION_H