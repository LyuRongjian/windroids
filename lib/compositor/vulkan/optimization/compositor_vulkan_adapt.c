/*
 * WinDroids Compositor
 * Vulkan Adaptive Rendering Quality Implementation
 */

#include "compositor_vulkan_adapt.h"
#include "compositor_vulkan_perf.h"
#include "compositor_vulkan_opt.h"
#include "compositor_utils.h"
#include <math.h>

// 动态调整渲染质量
void adapt_rendering_quality(VulkanState* vulkan) {
    if (!vulkan) {
        return;
    }
    
    // 获取性能统计数据
    VulkanPerfStats* perf_stats = &vulkan->perf_stats;
    VulkanRenderOptimization* optimization = &vulkan->optimization;
    
    // 计算平均FPS
    uint32_t current_fps = get_current_fps(perf_stats);
    double avg_frame_time = get_average_frame_time(perf_stats);
    
    // 根据FPS调整渲染质量
    if (current_fps < 30 && avg_frame_time > 33.3) {
        // FPS过低，降低渲染质量
        decrease_rendering_quality(vulkan);
    } else if (current_fps > 55 && avg_frame_time < 18.0) {
        // FPS良好，可以提高渲染质量
        increase_rendering_quality(vulkan);
    }
    
    // 根据内存使用情况调整
    size_t total_memory, peak_memory;
    get_memory_usage_stats(perf_stats, &total_memory, &peak_memory);
    
    // 如果内存使用过高，降低渲染质量
    if (total_memory > (vulkan->device_memory_limit * 0.8)) {
        decrease_rendering_quality(vulkan);
    }
}

// 降低渲染质量以提高性能
void decrease_rendering_quality(VulkanState* vulkan) {
    if (!vulkan) {
        return;
    }
    
    VulkanRenderOptimization* optimization = &vulkan->optimization;
    bool quality_changed = false;
    
    // 降低各向异性过滤
    if (optimization->max_anisotropy > 1.0f) {
        optimization->max_anisotropy = fmax(1.0f, optimization->max_anisotropy / 2.0f);
        quality_changed = true;
        log_message(COMPOSITOR_LOG_INFO, "Decreased anisotropic filtering to %.1f", optimization->max_anisotropy);
    }
    
    // 禁用MSAA或降低采样数
    if (optimization->enable_msaa && optimization->msaa_samples > 1) {
        optimization->msaa_samples = 1;
        optimization->enable_msaa = false;
        quality_changed = true;
        log_message(COMPOSITOR_LOG_INFO, "Disabled MSAA to improve performance");
    }
    
    // 禁用脏区域优化（如果启用）
    if (optimization->enable_dirty_rects) {
        optimization->enable_dirty_rects = false;
        quality_changed = true;
        log_message(COMPOSITOR_LOG_INFO, "Disabled dirty region optimization to improve performance");
    }
    
    // 降低纹理缓存大小
    if (vulkan->texture_cache.max_textures > 50) {
        vulkan->texture_cache.max_textures = 50;
        quality_changed = true;
        log_message(COMPOSITOR_LOG_INFO, "Reduced texture cache size to improve performance");
    }
    
    // 如果有调整，记录日志
    if (quality_changed) {
        log_message(COMPOSITOR_LOG_INFO, "Rendering quality decreased to maintain performance");
    }
}

// 提高渲染质量以改善视觉效果
void increase_rendering_quality(VulkanState* vulkan) {
    if (!vulkan) {
        return;
    }
    
    VulkanRenderOptimization* optimization = &vulkan->optimization;
    bool quality_changed = false;
    
    // 提高各向异性过滤（如果低于最大值）
    if (optimization->max_anisotropy < 16.0f) {
        optimization->max_anisotropy = fmin(16.0f, optimization->max_anisotropy * 2.0f);
        quality_changed = true;
        log_message(COMPOSITOR_LOG_INFO, "Increased anisotropic filtering to %.1f", optimization->max_anisotropy);
    }
    
    // 启用MSAA（如果禁用）
    if (!optimization->enable_msaa) {
        optimization->enable_msaa = true;
        optimization->msaa_samples = 4;
        quality_changed = true;
        log_message(COMPOSITOR_LOG_INFO, "Enabled MSAA to improve quality");
    }
    
    // 启用脏区域优化（如果禁用）
    if (!optimization->enable_dirty_rects) {
        optimization->enable_dirty_rects = true;
        quality_changed = true;
        log_message(COMPOSITOR_LOG_INFO, "Enabled dirty region optimization to improve quality");
    }
    
    // 增加纹理缓存大小（如果低于最大值）
    if (vulkan->texture_cache.max_textures < 500) {
        vulkan->texture_cache.max_textures = 500;
        quality_changed = true;
        log_message(COMPOSITOR_LOG_INFO, "Increased texture cache size to improve quality");
    }
    
    // 如果有调整，记录日志
    if (quality_changed) {
        log_message(COMPOSITOR_LOG_INFO, "Rendering quality increased to improve visual quality");
    }
}