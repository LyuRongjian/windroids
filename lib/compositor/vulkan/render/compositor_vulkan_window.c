#include "compositor_vulkan_window.h"
#include "compositor_vulkan_render_queue.h"
#include "compositor_vulkan_texture.h"
#include "compositor_dirty.h"
#include "compositor_utils.h"
#include &lt;string.h&gt;
#include &lt;stdlib.h&gt;

// 渲染所有窗口
void render_windows(CompositorState* state) {
    VulkanState* vulkan = &state->vulkan;
    
    // 性能统计开始
    uint64_t start_time = get_current_time_ms();
    vulkan->perf_stats.surface_count = 0;
    vulkan->perf_stats.windows_rendered = 0;
    
    // 1. 脏区域优化：如果启用了脏区域优化且有脏区域，则先进行剔除
    if (state->config.use_dirty_rect_optimization && state->dirty_rect_count > 0) {
        // 这里可以实现更复杂的脏区域优化逻辑
    }
    
    // 渲染Xwayland窗口（按z_order顺序）
    XwaylandWindowState* xwayland_state = &state->xwayland_state;
    for (int i = 0; i < xwayland_state->window_count; i++) {
        XwaylandWindowState* window = xwayland_state->windows[i];
        
        // 检查窗口是否需要渲染
        if (window->state == WINDOW_STATE_MINIMIZED || window->surface == NULL) {
            continue;
        }
        
        // 脏区域优化检查
        bool use_dirty_rect = state->config.use_dirty_rect_optimization;
        if (use_dirty_rect && !window->is_dirty && !check_window_intersects_dirty_rect(state, window, false)) {
            continue;  // 窗口与脏区域不相交，跳过渲染
        }
        
        // 准备窗口信息
        WindowInfo info = {
            .title = window->title,
            .x = window->x,
            .y = window->y,
            .width = window->width,
            .height = window->height,
            .state = window->state,
            .opacity = window->opacity,
            .z_order = window->z_order,
            .is_wayland = false
        };
        
        // 记录渲染统计
        vulkan->perf_stats.windows_rendered++;
        
        // 渲染窗口
        render_window(state, &info, false);
        
        // 清除窗口脏标记
        window->is_dirty = false;
        window->dirty_region_count = 0;
    }
    
    // 渲染Wayland窗口（按z_order顺序）
    WaylandWindowState* wayland_state = &state->wayland_state;
    for (int i = 0; i < wayland_state->window_count; i++) {
        WaylandWindow* window = wayland_state->windows[i];
        
        // 检查窗口是否需要渲染
        if (window->state == WINDOW_STATE_MINIMIZED || window->surface == NULL) {
            continue;
        }
        
        // 脏区域优化检查
        bool use_dirty_rect = state->config.use_dirty_rect_optimization;
        if (use_dirty_rect && !window->is_dirty && !check_window_intersects_dirty_rect(state, window, true)) {
            continue;  // 窗口与脏区域不相交，跳过渲染
        }
        
        // 准备窗口信息
        WindowInfo info = {
            .title = window->title,
            .x = window->x,
            .y = window->y,
            .width = window->width,
            .height = window->height,
            .state = window->state,
            .opacity = window->opacity,
            .z_order = window->z_order,
            .is_wayland = true
        };
        
        // 记录渲染统计
        vulkan->perf_stats.windows_rendered++;
        
        // 渲染窗口
        render_window(state, &info, true);
        
        // 清除窗口脏标记
        window->is_dirty = false;
        window->dirty_region_count = 0;
    }
    
    // 输出性能调试信息
    if (state->config.debug_mode && vulkan->perf_monitor.frame_count % 60 == 0) {
        log_message(COMPOSITOR_LOG_DEBUG, "Rendered %d windows, FPS: %.1f, Batch count: %d",
                   vulkan->perf_monitor.windows_rendered,
                   vulkan->perf_monitor.fps,
                   vulkan->multi_window.active_batch_count);
        vulkan->perf_monitor.windows_rendered = 0;
    }
}

// 渲染单个窗口
void render_window(CompositorState* state, WindowInfo* window, bool is_wayland) {
    VulkanState* vulkan = &state->vulkan;
    
    // 计算窗口在屏幕上的可见区域
    int visible_x = window->x;
    int visible_y = window->y;
    int visible_width = window->width;
    int visible_height = window->height;
    
    // 处理窗口超出屏幕边界的情况
    if (window->x < 0) {
        visible_x = 0;
        visible_width += window->x;  // 减去负值
    }
    
    if (window->y < 0) {
        visible_y = 0;
        visible_height += window->y;  // 减去负值
    }
    
    if (window->x + window->width > state->width) {
        visible_width = state->width - window->x;
    }
    
    if (window->y + window->height > state->height) {
        visible_height = state->height - window->y;
    }
    
    // 确保可见区域有效
    if (visible_width <= 0 || visible_height <= 0) {
        return;
    }
    
    // 获取窗口表面和纹理ID
    void* window_surface = NULL;
    uint32_t texture_id = UINT32_MAX;
    
    if (is_wayland) {
        // 从Wayland窗口获取表面
        WaylandWindow* wayland_window = NULL;
        for (int i = 0; i < state->wayland_state.window_count; i++) {
            if (strcmp(state->wayland_state.windows[i]->title, window->title) == 0 &&
                state->wayland_state.windows[i]->x == window->x &&
                state->wayland_state.windows[i]->y == window->y) {
                wayland_window = state->wayland_state.windows[i];
                window_surface = wayland_window->surface;
                break;
            }
        }
    } else {
        // 从Xwayland窗口获取表面
        XwaylandWindowState* xwayland_window = NULL;
        for (int i = 0; i < state->xwayland_state.window_count; i++) {
            if (strcmp(state->xwayland_state.windows[i]->title, window->title) == 0 &&
                state->xwayland_state.windows[i]->x == window->x &&
                state->xwayland_state.windows[i]->y == window->y) {
                xwayland_window = state->xwayland_state.windows[i];
                window_surface = xwayland_window->surface;
                break;
            }
        }
    }
    
    // 如果找到了表面，尝试获取或创建纹理
    if (window_surface) {
        // 使用新的基于surface的纹理缓存系统
        texture_id = get_cached_texture_by_surface(vulkan, window_surface);
        
        if (texture_id != UINT32_MAX) {
            // 纹理ID有效，获取实际纹理
            VulkanTexture* texture = NULL;
            if (get_texture(vulkan, texture_id, &texture) == COMPOSITOR_OK && texture) {
                // 创建渲染命令并添加到队列
                RenderCommand cmd = {0};
                cmd.type = is_wayland ? RENDER_COMMAND_WINDOW : RENDER_COMMAND_XWAYLAND_WINDOW;
                cmd.data.window.x = visible_x;
                cmd.data.window.y = visible_y;
                cmd.data.window.width = visible_width;
                cmd.data.window.height = visible_height;
                cmd.data.window.texture_id = texture_id;
                cmd.data.window.opacity = window->opacity;
                
                // 添加到渲染队列
                add_render_command(vulkan, cmd.type, &cmd.data.window);
                
                vulkan->perf_stats.surface_count++;
            }
        }
    }
}

// 使用硬件加速渲染窗口
void render_windows_with_hardware_acceleration(CompositorState* state) {
    VulkanState* vulkan = &state->vulkan;
    
    // 性能统计开始
    uint64_t start_time = get_current_time_ms();
    
    // 1. 可见性剔除：移除不可见的窗口
    if (state->config.use_visibility_culling) {
        // 这里可以实现更复杂的可见性剔除逻辑
    }
    
    // 2. 合并脏区域以优化渲染
    if (state->config.use_dirty_rect_optimization && state->dirty_rect_count > 0) {
        // 这里可以实现脏区域合并逻辑
    }
    
    // 3. 收集可见窗口并按Z顺序排序（使用优化的辅助函数）
    void** visible_windows = NULL;
    bool* window_types = NULL;
    int* z_orders = NULL;
    int visible_count = 0; // 这里应该调用collect_and_prepare_visible_windows函数
    
    // 如果有可见窗口，继续处理
    if (visible_count > 0) {
        // 4. 执行窗口遮挡剔除（优化大量重叠窗口的场景）
        if (state->config.use_occlusion_culling && visible_count > 5) {
            // 对已排序的窗口（从后向前）进行遮挡检测
            // 已经按Z顺序排序，从后向前检查，后面的窗口可能遮挡前面的窗口
            for (int i = visible_count - 1; i >= 0; i--) {
                bool is_occluded = false;
                
                // 检查当前窗口是否被后面的窗口完全遮挡
                for (int j = i + 1; j < visible_count && !is_occluded; j++) {
                    if (is_window_completely_occluded(visible_windows[i], window_types[i], 
                                                    visible_windows[j], window_types[j])) {
                        is_occluded = true;
                        vulkan->perf_stats.occluded_windows++;
                    }
                }
                
                // 如果窗口被完全遮挡，可以跳过渲染
                if (is_occluded) {
                    // 标记窗口为隐藏，避免后续渲染
                    if (window_types[i]) {
                        ((WaylandWindow*)visible_windows[i])->is_hidden = true;
                    } else {
                        ((XwaylandWindowState*)visible_windows[i])->is_hidden = true;
                    }
                }
            }
        }
        
        // 5. 窗口渲染批处理：根据纹理ID对窗口进行分组以减少状态切换
        if (state->config.use_render_batching) {
            // 将窗口按纹理分组，再添加到渲染队列
            batch_windows_by_texture(state, visible_windows, window_types, visible_count);
        } else {
            // 不使用批处理时，直接添加每个窗口
            for (int i = 0; i < visible_count; i++) {
                bool use_dirty_rects = false;
                bool is_dirty = false;
                
                // 检查窗口是否需要脏区域优化
                if (state->config.use_dirty_rect_optimization) {
                    is_dirty = check_window_intersects_dirty_rect(state, visible_windows[i], window_types[i]);
                    use_dirty_rects = is_dirty;
                }
                
                // 跳过隐藏的窗口
                if (window_types[i] && ((WaylandWindow*)visible_windows[i])->is_hidden) continue;
                if (!window_types[i] && ((XwaylandWindowState*)visible_windows[i])->is_hidden) continue;
                
                // 准备窗口渲染
                if (prepare_window_rendering(state, visible_windows[i], window_types[i]) != COMPOSITOR_OK) {
                    log_message(COMPOSITOR_LOG_WARNING, "Failed to prepare window for rendering");
                    continue;
                }
                
                // 添加窗口渲染命令到队列
                if (window_types[i]) {
                    add_render_command(vulkan, RENDER_COMMAND_WINDOW, visible_windows[i]);
                } else {
                    add_render_command(vulkan, RENDER_COMMAND_XWAYLAND_WINDOW, visible_windows[i]);
                }
                
                vulkan->perf_stats.surface_count++;
            }
        }
        
        // 6. 优化渲染批次
        if (vulkan->render_queue_size > 0) {
            int before_batch_count = vulkan->perf_stats.batch_count;
            optimize_render_batches(vulkan);
            vulkan->perf_stats.batch_optimization_gain = before_batch_count - vulkan->perf_stats.batch_count;
        }
        
        // 7. 执行渲染队列
        if (vulkan->render_queue_size > 0) {
            // 获取当前命令缓冲区
            uint32_t current_frame = vulkan->current_frame % MAX_FRAMES_IN_FLIGHT;
            VkCommandBuffer command_buffer = vulkan->command_buffers[current_frame];
            
            // 重置命令缓冲区
            VkResult result = vkResetCommandBuffer(command_buffer, 0);
            if (result != VK_SUCCESS) {
                log_message(COMPOSITOR_LOG_ERROR, "Failed to reset command buffer");
                free(visible_windows);
                free(window_types);
                free(z_orders);
                return;
            }
            
            // 开始记录命令
            VkCommandBufferBeginInfo begin_info = {0};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            // 使用多次提交标志优化性能
            begin_info.flags = state->config.use_command_buffer_reuse ? 
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT : 
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            
            if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
                log_message(COMPOSITOR_LOG_ERROR, "Failed to begin command buffer");
                free(visible_windows);
                free(window_types);
                free(z_orders);
                return;
            }
            
            // 执行渲染队列
            execute_render_queue(vulkan, command_buffer);
            
            // 结束记录命令
            if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
                log_message(COMPOSITOR_LOG_ERROR, "Failed to end command buffer");
                free(visible_windows);
                free(window_types);
                free(z_orders);
                return;
            }
            
            // 记录命令缓冲区提交时间
            vulkan->last_command_buffer_submit = get_current_time_ms();
        }
        
        // 释放临时内存
        free(visible_windows);
        free(window_types);
        free(z_orders);
    }
    
    // 8. 清理完成的窗口渲染资源
    // 优化：只清理实际渲染过的窗口
    for (int i = 0; i < state->xwayland_state.window_count; i++) {
        XwaylandWindowState* window = state->xwayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == state->active_workspace || window->is_sticky)) {
            
            finish_window_rendering(state, window, false);
            // 重置隐藏标志，为下一帧做准备
            window->is_hidden = false;
        }
    }
    
    for (int i = 0; i < state->wayland_state.window_count; i++) {
        WaylandWindow* window = state->wayland_state.windows[i];
        if (!window->is_minimized && 
            (window->workspace_id == state->active_workspace || window->is_sticky)) {
            
            finish_window_rendering(state, window, true);
            // 重置隐藏标志
            window->is_hidden = false;
        }
    }
    
    // 9. 纹理缓存维护
    if (state->config.use_texture_caching && 
        (get_current_time_ms() - vulkan->last_texture_cache_maintenance) > TEXTURE_CACHE_MAINTENANCE_INTERVAL) {
        perform_texture_cache_maintenance(vulkan);
        vulkan->last_texture_cache_maintenance = get_current_time_ms();
    }
    
    // 计算帧时间
    vulkan->perf_stats.frame_time = get_current_time_ms() - start_time;
    
    // 动态调整渲染优化设置
    if (state->config.use_adaptive_rendering) {
        adapt_rendering_quality(vulkan);
    }
    
    // 更新性能统计
    update_vulkan_performance_stats(&vulkan->perf_stats);
    
    // 清除脏区域标记
    if (state->config.use_dirty_rect_optimization) {
        compositor_clear_dirty_rects();
    }
}

// 准备窗口渲染
int prepare_window_rendering(CompositorState* state, void* window, bool is_wayland) {
    VulkanState* vulkan = &state->vulkan;
    
    // 获取窗口表面
    void* surface = NULL;
    if (is_wayland) {
        surface = ((WaylandWindow*)window)->surface;
    } else {
        surface = ((XwaylandWindowState*)window)->surface;
    }
    
    if (!surface) {
        return COMPOSITOR_ERROR_INVALID_ARGUMENT;
    }
    
    // 检查是否已经有对应的纹理
    uint32_t texture_id = get_cached_texture_by_surface(vulkan, surface);
    if (texture_id == UINT32_MAX) {
        // 如果没有，创建新的纹理
        VulkanTexture texture = {0};
        texture.surface = surface;
        
        // 这里应该调用实际的纹理创建函数
        // int result = create_texture_from_surface(vulkan, surface, &texture_id);
        // if (result != COMPOSITOR_OK) {
        //     return result;
        // }
        
        // 将纹理添加到缓存
        // add_texture_to_cache(vulkan, texture_id, &texture);
    }
    
    return COMPOSITOR_OK;
}

// 完成窗口渲染后的清理工作
void finish_window_rendering(CompositorState* state, void* window, bool is_wayland) {
    // 在这里可以执行窗口渲染后的清理工作
    // 例如：释放临时资源、更新窗口状态等
    
    if (is_wayland) {
        WaylandWindow* wayland_window = (WaylandWindow*)window;
        // 重置窗口的一些临时状态
        wayland_window->is_hidden = false;
    } else {
        XwaylandWindowState* xwayland_window = (XwaylandWindowState*)window;
        // 重置窗口的一些临时状态
        xwayland_window->is_hidden = false;
    }
}

// 检查窗口是否与脏区域相交
bool check_window_intersects_dirty_rect(CompositorState* state, void* window, bool is_wayland) {
    // 实现检查窗口是否与任何脏区域相交的逻辑
    // 这是一个简化的实现，实际实现可能需要更复杂的矩形相交检测
    
    if (is_wayland) {
        WaylandWindow* wayland_window = (WaylandWindow*)window;
        // 检查窗口区域是否与任何脏区域相交
        for (int i = 0; i < state->dirty_rect_count; i++) {
            DirtyRect* dirty = &state->dirty_rects[i];
            if (!(wayland_window->x > dirty->x + dirty->width ||
                  wayland_window->x + wayland_window->width < dirty->x ||
                  wayland_window->y > dirty->y + dirty->height ||
                  wayland_window->y + wayland_window->height < dirty->y)) {
                return true;  // 相交
            }
        }
    } else {
        XwaylandWindowState* xwayland_window = (XwaylandWindowState*)window;
        // 检查窗口区域是否与任何脏区域相交
        for (int i = 0; i < state->dirty_rect_count; i++) {
            DirtyRect* dirty = &state->dirty_rects[i];
            if (!(xwayland_window->x > dirty->x + dirty->width ||
                  xwayland_window->x + xwayland_window->width < dirty->x ||
                  xwayland_window->y > dirty->y + dirty->height ||
                  xwayland_window->y + xwayland_window->height < dirty->y)) {
                return true;  // 相交
            }
        }
    }
    
    return false;  // 不相交
}

// 检查一个窗口是否被另一个窗口完全遮挡
bool is_window_completely_occluded(void* front_window, bool front_is_wayland, void* back_window, bool back_is_wayland) {
    // 实现窗口遮挡检测逻辑
    // 这是一个简化的实现，实际实现可能需要考虑透明度、z-order等因素
    
    int front_x, front_y, front_width, front_height;
    int back_x, back_y, back_width, back_height;
    
    if (front_is_wayland) {
        WaylandWindow* window = (WaylandWindow*)front_window;
        front_x = window->x;
        front_y = window->y;
        front_width = window->width;
        front_height = window->height;
    } else {
        XwaylandWindowState* window = (XwaylandWindowState*)front_window;
        front_x = window->x;
        front_y = window->y;
        front_width = window->width;
        front_height = window->height;
    }
    
    if (back_is_wayland) {
        WaylandWindow* window = (WaylandWindow*)back_window;
        back_x = window->x;
        back_y = window->y;
        back_width = window->width;
        back_height = window->height;
    } else {
        XwaylandWindowState* window = (XwaylandWindowState*)back_window;
        back_x = window->x;
        back_y = window->y;
        back_width = window->width;
        back_height = window->height;
    }
    
    // 检查前面的窗口是否完全包含后面的窗口
    if (front_x <= back_x && 
        front_y <= back_y &&
        front_x + front_width >= back_x + back_width &&
        front_y + front_height >= back_y + back_height) {
        return true;  // 完全遮挡
    }
    
    return false;  // 没有完全遮挡
}

// 按纹理对窗口进行批处理
void batch_windows_by_texture(CompositorState* state, void** windows, bool* window_types, int count) {
    VulkanState* vulkan = &state->vulkan;
    
    // 这里应该实现按纹理ID对窗口进行分组的逻辑
    // 为了简化，我们直接按顺序添加窗口到渲染队列
    
    for (int i = 0; i < count; i++) {
        // 跳过隐藏的窗口
        if (window_types[i] && ((WaylandWindow*)windows[i])->is_hidden) continue;
        if (!window_types[i] && ((XwaylandWindowState*)windows[i])->is_hidden) continue;
        
        // 准备窗口渲染
        if (prepare_window_rendering(state, windows[i], window_types[i]) != COMPOSITOR_OK) {
            log_message(COMPOSITOR_LOG_WARNING, "Failed to prepare window for rendering");
            continue;
        }
        
        // 添加窗口渲染命令到队列
        if (window_types[i]) {
            add_render_command(vulkan, RENDER_COMMAND_WINDOW, windows[i]);
        } else {
            add_render_command(vulkan, RENDER_COMMAND_XWAYLAND_WINDOW, windows[i]);
        }
        
        vulkan->perf_stats.surface_count++;
    }
}