#ifndef COMPOSITOR_INPUT_SHORTCUTS_H
#define COMPOSITOR_INPUT_SHORTCUTS_H

#include "compositor_input_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// 快捷键初始化和清理
int compositor_input_shortcuts_init(void);
void compositor_input_shortcuts_cleanup(void);

// 快捷键处理函数
void handle_keyboard_shortcuts(int keycode, int state, int modifiers);
void handle_enhanced_keyboard_shortcuts(int keycode, int state, int modifiers);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_INPUT_SHORTCUTS_H