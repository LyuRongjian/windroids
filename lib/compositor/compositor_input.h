#ifndef COMPOSITOR_INPUT_H
#define COMPOSITOR_INPUT_H

// 输入事件类型
enum {
    COMPOSITOR_INPUT_NONE = 0,
    COMPOSITOR_INPUT_MOTION = 1,
    COMPOSITOR_INPUT_BUTTON = 2,
    COMPOSITOR_INPUT_KEY = 3,
    COMPOSITOR_INPUT_TOUCH = 4
};

// 输入状态
enum {
    COMPOSITOR_INPUT_STATE_UP = 0,
    COMPOSITOR_INPUT_STATE_DOWN = 1,
    COMPOSITOR_INPUT_STATE_MOVE = 2
};

// 注入输入事件（触摸、键盘等）
// - type: 事件类型（COMPOSITOR_INPUT_*）
// - x/y: 坐标位置
// - key/button: 键码或按钮
// - state: 状态（COMPOSITOR_INPUT_STATE_*）
void compositor_handle_input(int type, int x, int y, int key, int state);

#endif // COMPOSITOR_INPUT_H