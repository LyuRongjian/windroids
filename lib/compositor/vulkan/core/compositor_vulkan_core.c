#include "compositor_vulkan_core.h"
#include <stdlib.h>
#include <stdio.h>

// 全局状态指针
static CompositorState* g_compositor_state = NULL;

// 设置全局状态指针
void compositor_vulkan_set_state(CompositorState* state) {
    g_compositor_state = state;
}

// 获取全局状态指针
CompositorState* compositor_vulkan_get_state(void) {
    return g_compositor_state;
}

// 获取Vulkan状态
VulkanState* get_vulkan_state(void) {
    if (g_compositor_state) {
        return &g_compositor_state->vulkan;
    }
    return NULL;
}