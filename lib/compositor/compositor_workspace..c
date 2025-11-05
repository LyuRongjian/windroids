/*
 * WinDroids Compositor
 * Workspace management module implementation
 */

#include "compositor_workspace..h"
#include "compositor_utils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// 全局状态引用
static CompositorState* g_compositor_state_ref = NULL;

// 设置合成器状态引用
void compositor_workspace_set_state(CompositorState* state) {
    g_compositor_state_ref = state;
}

// 查找窗口的辅助函数
void* find_window_by_title(const char* title, bool* out_is_wayland) {
    if (!g_compositor_state_ref || !title || !out_is_wayland) {
        return NULL;
    }
    
    // 检查Xwayland窗口
    for (int32_t i = 0; i < g_compositor_state_ref->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = g_compositor_state_ref->xwayland_state.windows[i];
        if (window && window->title && strcmp(window->title, title) == 0) {
            *out_is_wayland = false;
            return window;
        }
    }
    
    // 检查Wayland窗口
    for (int32_t i = 0; i < g_compositor_state_ref->wayland_state.window_count; i++) {
        WaylandWindow* window = g_compositor_state_ref->wayland_state.windows[i];
        if (window && window->title && strcmp(window->title, title) == 0) {
            *out_is_wayland = true;
            return window;
        }
    }
    
    return NULL;
}

// 创建新工作区
int compositor_create_workspace(const char* name) {
    if (!g_compositor_state_ref) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    // 重新分配工作区数组
    Workspace* new_workspaces = (Workspace*)safe_realloc(
        g_compositor_state_ref->workspaces, 
        (g_compositor_state_ref->workspace_count + 1) * sizeof(Workspace)
    );
    if (!new_workspaces) {
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    g_compositor_state_ref->workspaces = new_workspaces;
    Workspace* workspace = &g_compositor_state_ref->workspaces[g_compositor_state_ref->workspace_count];
    
    // 初始化工作区
    workspace->name = strdup(name ? name : "Untitled");
    if (!workspace->name) {
        return COMPOSITOR_ERROR_MEMORY;
    }
    workspace->is_active = (g_compositor_state_ref->workspace_count == 0);
    workspace->window_count = 0;
    workspace->windows = NULL;
    workspace->is_wayland = NULL;
    workspace->window_groups = NULL;
    workspace->group_count = 0;
    
    g_compositor_state_ref->workspace_count++;
    track_memory_allocation(sizeof(Workspace));
    track_memory_allocation(strlen(workspace->name) + 1);
    
    log_message(COMPOSITOR_LOG_INFO, "Created workspace '%s' (ID: %d)", 
            workspace->name, g_compositor_state_ref->workspace_count - 1);
    
    // 如果是第一个工作区，自动包含所有没有指定工作区的窗口
    if (g_compositor_state_ref->workspace_count == 1) {
        for (int i = 0; i < g_compositor_state_ref->xwayland_state.window_count; i++) {
            XwaylandWindowState* window = g_compositor_state_ref->xwayland_state.windows[i];
            if (window->workspace_id < 0) {
                window->workspace_id = 0;
            }
        }
        
        for (int i = 0; i < g_compositor_state_ref->wayland_state.window_count; i++) {
            WaylandWindow* window = g_compositor_state_ref->wayland_state.windows[i];
            if (window->workspace_id < 0) {
                window->workspace_id = 0;
            }
        }
    }
    
    return g_compositor_state_ref->workspace_count - 1;
}

// 切换工作区
int compositor_switch_workspace(int workspace_index) {
    if (!g_compositor_state_ref) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (workspace_index < 0 || workspace_index >= g_compositor_state_ref->workspace_count) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid workspace index: %d", workspace_index);
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 禁用当前工作区
    if (g_compositor_state_ref->active_workspace >= 0 && g_compositor_state_ref->active_workspace < g_compositor_state_ref->workspace_count) {
        g_compositor_state_ref->workspaces[g_compositor_state_ref->active_workspace].is_active = false;
    }
    
    // 激活新工作区
    g_compositor_state_ref->active_workspace = workspace_index;
    g_compositor_state_ref->workspaces[workspace_index].is_active = true;
    
    log_message(COMPOSITOR_LOG_INFO, "Switched to workspace '%s' (ID: %d)", 
            g_compositor_state_ref->workspaces[workspace_index].name, workspace_index);
    
    // 标记整个屏幕需要重绘
    compositor_schedule_redraw();
    
    return COMPOSITOR_OK;
}

// 将窗口移动到指定工作区
int compositor_move_window_to_workspace(const char* window_title, int workspace_index) {
    if (!g_compositor_state_ref) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (workspace_index < 0 || workspace_index >= g_compositor_state_ref->workspace_count) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid workspace index: %d", workspace_index);
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    if (!window_title || strlen(window_title) == 0) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid window title");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 使用辅助函数查找窗口
    bool is_wayland;
    void* window = find_window_by_title(window_title, &is_wayland);
    
    if (!window) {
        set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window '%s' not found", window_title);
        return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
    }
    
    // 更新窗口工作区ID
    if (is_wayland) {
        ((WaylandWindow*)window)->workspace_id = workspace_index;
    } else {
        ((XwaylandWindowState*)window)->workspace_id = workspace_index;
    }
    
    // 如果移动到当前工作区，确保窗口可见
    if (workspace_index == g_compositor_state_ref->active_workspace) {
        if (is_wayland) {
            ((WaylandWindow*)window)->is_minimized = false;
        } else {
            ((XwaylandWindowState*)window)->is_minimized = false;
        }
    }
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    log_message(COMPOSITOR_LOG_INFO, "Moved window '%s' to workspace %d", 
            window_title, workspace_index);
    
    return COMPOSITOR_OK;
}

// 收集当前工作区可见窗口
int collect_visible_windows(void*** windows, bool** is_wayland, int max_count) {
    if (!g_compositor_state_ref || !windows || !is_wayland) {
        return 0;
    }
    
    int count = 0;
    CompositorState* state = g_compositor_state_ref;
    int active_ws = state->active_workspace;
    
    // 收集Xwayland窗口
    for (int i = 0; i < state->xwayland_state.window_count && count < max_count; i++) {
        XwaylandWindowState* window = state->xwayland_state.windows[i];
        if (window && !window->is_minimized && 
            (window->workspace_id == active_ws || window->is_sticky)) {
            windows[count] = (void*)window;
            is_wayland[count] = false;
            count++;
        }
    }
    
    // 收集Wayland窗口
    for (int i = 0; i < state->wayland_state.window_count && count < max_count; i++) {
        WaylandWindow* window = state->wayland_state.windows[i];
        if (window && !window->is_minimized && 
            (window->workspace_id == active_ws || window->is_sticky)) {
            windows[count] = (void*)window;
            is_wayland[count] = true;
            count++;
        }
    }
    
    return count;
}

// 窗口平铺功能
int compositor_tile_windows(int tile_mode) {
    if (!g_compositor_state_ref) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (tile_mode != TILE_MODE_HORIZONTAL && tile_mode != TILE_MODE_VERTICAL && tile_mode != TILE_MODE_GRID) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid tile mode: %d", tile_mode);
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    CompositorState* state = g_compositor_state_ref;
    state->tile_mode = tile_mode;
    
    // 获取配置参数
    const int margin = state->config.window_margin > 0 ? state->config.window_margin : 4;
    const int decoration_size = state->config.window_decoration_size > 0 ? state->config.window_decoration_size : 24;
    const int min_width = state->config.min_window_width > 0 ? state->config.min_window_width : 300;
    const int min_height = state->config.min_window_height > 0 ? state->config.min_window_height : 200;
    
    // 计算可用空间
    const int available_width = state->width - margin * 2;
    const int available_height = state->height - margin * 2 - decoration_size;
    
    if (available_width < min_width || available_height < min_height) {
        log_message(COMPOSITOR_LOG_WARN, "Insufficient space for tiling: %dx%d < %dx%d",
                   available_width, available_height, min_width, min_height);
        return COMPOSITOR_ERROR_INSUFFICIENT_SPACE;
    }
    
    // 收集可见窗口
    const int max_windows = state->config.max_windows > 0 ? state->config.max_windows : 32;
    void** windows = (void**)alloca(max_windows * sizeof(void*));
    bool* is_wayland = (bool*)alloca(max_windows * sizeof(bool));
    int visible_count = collect_visible_windows(&windows, &is_wayland, max_windows);
    
    if (visible_count == 0) return COMPOSITOR_OK;
    
    // 根据平铺模式计算窗口大小和位置
    int tile_width = 0, tile_height = 0;
    int cols = 1, rows = 1;
    
    switch (tile_mode) {
        case TILE_MODE_HORIZONTAL:
            cols = visible_count;
            rows = 1;
            tile_width = (available_width - margin * (cols - 1)) / cols;
            tile_height = available_height;
            
            // 调整以确保最小宽度
            if (tile_width < min_width) {
                tile_width = min_width;
                cols = available_width / (tile_width + margin);
                if (cols < 1) cols = 1;
            }
            break;
            
        case TILE_MODE_VERTICAL:
            cols = 1;
            rows = visible_count;
            tile_width = available_width;
            tile_height = (available_height - margin * (rows - 1)) / rows;
            
            // 调整以确保最小高度
            if (tile_height < min_height) {
                tile_height = min_height;
                rows = available_height / (tile_height + margin);
                if (rows < 1) rows = 1;
            }
            break;
            
        case TILE_MODE_GRID:
            // 智能计算最佳行列数
            cols = (int)sqrt(visible_count);
            rows = (visible_count + cols - 1) / cols;
            
            // 优化行列比例以匹配屏幕宽高比
            float screen_ratio = (float)available_width / available_height;
            float ideal_ratio = cols > 0 ? (float)cols / rows : 1.0f;
            
            // 根据屏幕比例调整行列数
            if (screen_ratio > 1.5f && ideal_ratio < 1.0f) cols++;
            else if (screen_ratio < 0.75f && ideal_ratio > 1.0f) rows++;
            
            rows = (visible_count + cols - 1) / cols;
            tile_width = (available_width - margin * (cols - 1)) / cols;
            tile_height = (available_height - margin * (rows - 1)) / rows;
            
            // 确保最小尺寸
            while ((tile_width < min_width || tile_height < min_height) && cols > 1) {
                cols--;
                rows = (visible_count + cols - 1) / cols;
                tile_width = (available_width - margin * (cols - 1)) / cols;
                tile_height = (available_height - margin * (rows - 1)) / rows;
            }
            break;
    }
    
    // 最终检查
    if (tile_width < min_width) tile_width = min_width;
    if (tile_height < min_height) tile_height = min_height;
    
    // 应用平铺布局
    for (int i = 0; i < visible_count; i++) {
        void* window = windows[i];
        bool wayland = is_wayland[i];
        int32_t x, y;
        
        // 计算位置
        if (tile_mode == TILE_MODE_HORIZONTAL) {
            x = margin + i * (tile_width + margin);
            y = margin + decoration_size;
        } else if (tile_mode == TILE_MODE_VERTICAL) {
            x = margin;
            y = margin + decoration_size + i * (tile_height + margin);
        } else { // GRID
            int col = i % cols;
            int row = i / cols;
            x = margin + col * (tile_width + margin);
            y = margin + decoration_size + row * (tile_height + margin);
        }
        
        // 边界检查
        x = CLAMP(x, margin, state->width - margin - tile_width);
        y = CLAMP(y, margin + decoration_size, state->height - margin - tile_height);
        
        // 保存原始状态并应用新布局
        if (wayland) {
            WaylandWindow* wl_window = (WaylandWindow*)window;
            wl_window->saved_x = wl_window->x;
            wl_window->saved_y = wl_window->y;
            wl_window->saved_width = wl_window->width;
            wl_window->saved_height = wl_window->height;
            wl_window->saved_state = wl_window->state;
            
            wl_window->x = x;
            wl_window->y = y;
            wl_window->width = tile_width;
            wl_window->height = tile_height;
            wl_window->state = WINDOW_STATE_TILED;
        } else {
            XwaylandWindowState* xw_window = (XwaylandWindowState*)window;
            xw_window->saved_x = xw_window->x;
            xw_window->saved_y = xw_window->y;
            xw_window->saved_width = xw_window->width;
            xw_window->saved_height = xw_window->height;
            xw_window->saved_state = xw_window->state;
            
            xw_window->x = x;
            xw_window->y = y;
            xw_window->width = tile_width;
            xw_window->height = tile_height;
            xw_window->state = WINDOW_STATE_TILED;
        }
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Tiled %d windows in mode %d: %dx%d grid, %dx%d per window",
               visible_count, tile_mode, cols, rows, tile_width, tile_height);
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    return COMPOSITOR_OK;
}

// 窗口级联排列
int compositor_cascade_windows() {
    if (!g_compositor_state_ref) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    g_compositor_state_ref->tile_mode = TILE_MODE_NONE;
    
    // 获取当前工作区的可见窗口
    int visible_count = 0;
    int i;
    
    // 计算可见窗口数量
    for (i = 0; i < g_compositor_state_ref->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = g_compositor_state_ref->xwayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == g_compositor_state_ref->active_workspace || window->is_sticky)) {
            visible_count++;
        }
    }
    
    for (i = 0; i < g_compositor_state_ref->wayland_state.window_count; i++) {
        WaylandWindow* window = g_compositor_state_ref->wayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == g_compositor_state_ref->active_workspace || window->is_sticky)) {
            visible_count++;
        }
    }
    
    if (visible_count == 0) return COMPOSITOR_OK;
    
    // 动态计算偏移量，根据窗口数量调整
    int32_t base_offset_x = 20;
    int32_t base_offset_y = 20;
    int32_t max_offset_x = g_compositor_state_ref->width / 4; // 最大X偏移量为屏幕宽度的1/4
    int32_t max_offset_y = g_compositor_state_ref->height / 4; // 最大Y偏移量为屏幕高度的1/4
    
    // 当窗口数量较多时，减少偏移量
    int32_t offset_x = visible_count > 10 ? base_offset_x / 2 : base_offset_x;
    int32_t offset_y = visible_count > 10 ? base_offset_y / 2 : base_offset_y;
    
    // 应用级联布局
    int current_index = 0;
    
    // 级联Xwayland窗口
    for (i = 0; i < g_compositor_state_ref->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = g_compositor_state_ref->xwayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == g_compositor_state_ref->active_workspace || window->is_sticky)) {
            
            // 恢复原始大小
            window->width = window->saved_width ? window->saved_width : 800;
            window->height = window->saved_height ? window->saved_height : 600;
            
            // 限制窗口最大尺寸，确保级联效果良好
            if (window->width > g_compositor_state_ref->width * 0.8) {
                window->width = g_compositor_state_ref->width * 0.8;
            }
            if (window->height > g_compositor_state_ref->height * 0.8) {
                window->height = g_compositor_state_ref->height * 0.8;
            }
            
            // 计算级联位置，使用环绕算法避免偏移过大
            int32_t x = (current_index * offset_x) % (max_offset_x + 1);
            int32_t y = (current_index * offset_y) % (max_offset_y + 1);
            
            // 如果窗口数量超过环绕点，调整位置以避免重叠
            if (current_index > (max_offset_x / offset_x) || current_index > (max_offset_y / offset_y)) {
                int wrap_factor = current_index / ((max_offset_x / offset_x) + 1);
                x = (current_index % ((max_offset_x / offset_x) + 1)) * offset_x;
                y = (current_index % ((max_offset_y / offset_y) + 1)) * offset_y + wrap_factor * 50;
            }
            
            // 确保窗口在屏幕范围内
            if (x + window->width > g_compositor_state_ref->width) {
                x = g_compositor_state_ref->width - window->width - 10;
            }
            if (y + window->height > g_compositor_state_ref->height) {
                y = g_compositor_state_ref->height - window->height - 10;
            }
            
            // 保存原始状态
            window->saved_x = window->x;
            window->saved_y = window->y;
            window->saved_width = window->width;
            window->saved_height = window->height;
            window->saved_state = window->state;
            
            window->x = x;
            window->y = y;
            window->state = WINDOW_STATE_NORMAL;
            
            current_index++;
        }
    }
    
    // 级联Wayland窗口
    for (i = 0; i < g_compositor_state_ref->wayland_state.window_count; i++) {
        WaylandWindow* window = g_compositor_state_ref->wayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == g_compositor_state_ref->active_workspace || window->is_sticky)) {
            
            // 恢复原始大小
            window->width = window->saved_width ? window->saved_width : 800;
            window->height = window->saved_height ? window->saved_height : 600;
            
            // 限制窗口最大尺寸，确保级联效果良好
            if (window->width > g_compositor_state_ref->width * 0.8) {
                window->width = g_compositor_state_ref->width * 0.8;
            }
            if (window->height > g_compositor_state_ref->height * 0.8) {
                window->height = g_compositor_state_ref->height * 0.8;
            }
            
            // 计算级联位置，使用环绕算法避免偏移过大
            int32_t x = (current_index * offset_x) % (max_offset_x + 1);
            int32_t y = (current_index * offset_y) % (max_offset_y + 1);
            
            // 如果窗口数量超过环绕点，调整位置以避免重叠
            if (current_index > (max_offset_x / offset_x) || current_index > (max_offset_y / offset_y)) {
                int wrap_factor = current_index / ((max_offset_x / offset_x) + 1);
                x = (current_index % ((max_offset_x / offset_x) + 1)) * offset_x;
                y = (current_index % ((max_offset_y / offset_y) + 1)) * offset_y + wrap_factor * 50;
            }
            
            // 确保窗口在屏幕范围内
            if (x + window->width > g_compositor_state_ref->width) {
                x = g_compositor_state_ref->width - window->width - 10;
            }
            if (y + window->height > g_compositor_state_ref->height) {
                y = g_compositor_state_ref->height - window->height - 10;
            }
            
            // 保存原始状态
            window->saved_x = window->x;
            window->saved_y = window->y;
            window->saved_width = window->width;
            window->saved_height = window->height;
            window->saved_state = window->state;
            
            window->x = x;
            window->y = y;
            window->state = WINDOW_STATE_NORMAL;
            
            current_index++;
        }
    }
    
    log_message(COMPOSITOR_LOG_DEBUG, "Cascaded %d windows", visible_count);
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    return COMPOSITOR_OK;
}

// 创建窗口组
int compositor_group_windows(const char** window_titles, int count, const char* group_name) {
    if (!g_compositor_state_ref) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (!window_titles || count <= 0 || !group_name || strlen(group_name) == 0) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid arguments");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 获取当前工作区
    int active_workspace = g_compositor_state_ref->active_workspace;
    if (active_workspace < 0 || active_workspace >= g_compositor_state_ref->workspace_count) {
        set_error(COMPOSITOR_ERROR_INVALID_STATE, "No active workspace");
        return COMPOSITOR_ERROR_INVALID_STATE;
    }
    
    Workspace* workspace = &g_compositor_state_ref->workspaces[active_workspace];
    
    // 检查组名是否已存在
    for (int i = 0; i < workspace->group_count; i++) {
        if (workspace->window_groups[i].name && 
            strcmp(workspace->window_groups[i].name, group_name) == 0) {
            set_error(COMPOSITOR_ERROR_GROUP_EXISTS, "Window group '%s' already exists", group_name);
            return COMPOSITOR_ERROR_GROUP_EXISTS;
        }
    }
    
    // 创建新的窗口组
    WindowGroup* new_groups = (WindowGroup*)safe_realloc(
        workspace->window_groups, 
        (workspace->group_count + 1) * sizeof(WindowGroup)
    );
    if (!new_groups) {
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    workspace->window_groups = new_groups;
    WindowGroup* group = &workspace->window_groups[workspace->group_count];
    
    // 初始化窗口组
    memset(group, 0, sizeof(WindowGroup)); // 确保所有字段初始化为0
    group->name = strdup(group_name);
    if (!group->name) {
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    // 首先尝试查找所有窗口，计算实际添加的数量
    int actual_count = 0;
    bool* found_windows = (bool*)alloca(count * sizeof(bool));
    memset(found_windows, 0, count * sizeof(bool));
    
    // 查找Xwayland窗口
    for (int i = 0; i < g_compositor_state_ref->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = g_compositor_state_ref->xwayland_state.windows[i];
        if (!window || !window->title || 
            (window->workspace_id != active_workspace && !window->is_sticky)) {
            continue;
        }
        
        for (int j = 0; j < count; j++) {
            if (!found_windows[j] && strcmp(window->title, window_titles[j]) == 0) {
                found_windows[j] = true;
                actual_count++;
                break;
            }
        }
    }
    
    // 查找Wayland窗口
    for (int i = 0; i < g_compositor_state_ref->wayland_state.window_count; i++) {
        WaylandWindow* window = g_compositor_state_ref->wayland_state.windows[i];
        if (!window || !window->title || 
            (window->workspace_id != active_workspace && !window->is_sticky)) {
            continue;
        }
        
        for (int j = 0; j < count; j++) {
            if (!found_windows[j] && strcmp(window->title, window_titles[j]) == 0) {
                found_windows[j] = true;
                actual_count++;
                break;
            }
        }
    }
    
    if (actual_count == 0) {
        free(group->name);
        set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "No windows found for grouping");
        return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
    }
    
    // 分配实际需要的窗口指针数组
    group->window_ptrs = (void**)safe_malloc(actual_count * sizeof(void*));
    group->is_wayland = (bool*)safe_malloc(actual_count * sizeof(bool));
    if (!group->window_ptrs || !group->is_wayland) {
        free(group->name);
        free(group->window_ptrs);
        free(group->is_wayland);
        return COMPOSITOR_ERROR_MEMORY;
    }
    
    // 填充窗口数组
    int added_count = 0;
    for (int i = 0; i < count; i++) {
        if (!found_windows[i]) continue;
        
        const char* title = window_titles[i];
        bool is_wayland;
        void* window = find_window_by_title(title, &is_wayland);
        
        if (window) {
            group->window_ptrs[added_count] = window;
            group->is_wayland[added_count] = is_wayland;
            
            // 设置窗口组ID
            if (is_wayland) {
                ((WaylandWindow*)window)->group_id = workspace->group_count;
            } else {
                ((XwaylandWindowState*)window)->group_id = workspace->group_count;
            }
            
            added_count++;
        }
    }
    
    group->window_count = added_count;
    
    // 记录内存分配
    track_memory_allocation(sizeof(WindowGroup));
    track_memory_allocation(strlen(group->name) + 1);
    track_memory_allocation(actual_count * sizeof(void*));
    track_memory_allocation(actual_count * sizeof(bool));
    
    workspace->group_count++;
    
    log_message(COMPOSITOR_LOG_INFO, "Created window group '%s' with %d windows (found %d of %d requested)", 
            group_name, added_count, added_count, count);
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    return COMPOSITOR_OK;
}

// 取消窗口组
int compositor_ungroup_windows(const char* group_name) {
    if (!g_compositor_state_ref) {
        set_error(COMPOSITOR_ERROR_NOT_INITIALIZED, "Compositor not initialized");
        return COMPOSITOR_ERROR_NOT_INITIALIZED;
    }
    
    if (!group_name || strlen(group_name) == 0) {
        set_error(COMPOSITOR_ERROR_INVALID_ARGS, "Invalid group name");
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    // 获取当前工作区
    int active_workspace = g_compositor_state_ref->active_workspace;
    if (active_workspace < 0 || active_workspace >= g_compositor_state_ref->workspace_count) {
        set_error(COMPOSITOR_ERROR_INVALID_STATE, "No active workspace");
        return COMPOSITOR_ERROR_INVALID_STATE;
    }
    
    Workspace* workspace = &g_compositor_state_ref->workspaces[active_workspace];
    
    // 查找窗口组
    int group_index = -1;
    for (int i = 0; i < workspace->group_count; i++) {
        if (workspace->window_groups[i].name && 
            strcmp(workspace->window_groups[i].name, group_name) == 0) {
            group_index = i;
            break;
        }
    }
    
    if (group_index == -1) {
        set_error(COMPOSITOR_ERROR_WINDOW_NOT_FOUND, "Window group '%s' not found", group_name);
        return COMPOSITOR_ERROR_WINDOW_NOT_FOUND;
    }
    
    // 清理窗口组中的窗口
    WindowGroup* group = &workspace->window_groups[group_index];
    int group_id = group_index;
    int window_count = group->window_count;
    
    // 重置窗口的组ID
    for (int i = 0; i < window_count; i++) {
        if (!group->window_ptrs[i]) continue;
        
        if (group->is_wayland[i]) {
            WaylandWindow* window = (WaylandWindow*)group->window_ptrs[i];
            if (window) {
                window->group_id = -1; // -1表示未分组
            }
        } else {
            XwaylandWindowState* window = (XwaylandWindowState*)group->window_ptrs[i];
            if (window) {
                window->group_id = -1; // -1表示未分组
            }
        }
    }
    
    // 释放窗口组资源
    size_t name_len = 0;
    if (group->name) {
        name_len = strlen(group->name) + 1;
        free(group->name);
        group->name = NULL;
    }
    
    if (group->window_ptrs) {
        free(group->window_ptrs);
        group->window_ptrs = NULL;
    }
    
    if (group->is_wayland) {
        free(group->is_wayland);
        group->is_wayland = NULL;
    }
    
    // 更新内存跟踪
    track_memory_free(sizeof(WindowGroup));
    if (name_len > 0) {
        track_memory_free(name_len);
    }
    track_memory_free(window_count * sizeof(void*));
    track_memory_free(window_count * sizeof(bool));
    
    // 将后面的窗口组前移
    if (workspace->group_count > 1 && group_index < workspace->group_count - 1) {
        memmove(&workspace->window_groups[group_index], 
               &workspace->window_groups[group_index + 1], 
               (workspace->group_count - group_index - 1) * sizeof(WindowGroup));
    }
    
    // 减少窗口组计数
    workspace->group_count--;
    
    // 调整剩余窗口组ID
    for (int i = 0; i < g_compositor_state_ref->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = g_compositor_state_ref->xwayland_state.windows[i];
        if (window && window->group_id > group_id) {
            window->group_id--;
        }
    }
    
    for (int i = 0; i < g_compositor_state_ref->wayland_state.window_count; i++) {
        WaylandWindow* window = g_compositor_state_ref->wayland_state.windows[i];
        if (window && window->group_id > group_id) {
            window->group_id--;
        }
    }
    
    // 重新分配窗口组数组
    if (workspace->group_count == 0) {
        if (workspace->window_groups) {
            free(workspace->window_groups);
            workspace->window_groups = NULL;
        }
    } else {
        WindowGroup* new_groups = (WindowGroup*)safe_realloc(
            workspace->window_groups, 
            workspace->group_count * sizeof(WindowGroup)
        );
        if (new_groups != NULL) {
            workspace->window_groups = new_groups;
        } else {
            log_message(COMPOSITOR_LOG_WARN, "Failed to realloc window groups array, memory may be wasted");
        }
    }
    
    log_message(COMPOSITOR_LOG_INFO, "Ungrouped window group '%s' with %d windows", group_name, window_count);
    
    // 标记需要重绘
    compositor_schedule_redraw();
    
    return COMPOSITOR_OK;
}