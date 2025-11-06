#include "compositor_module_utils.h"
#include "compositor_utils.h"
#include <string.h>

// 批量初始化模块
int compositor_init_modules(const ModuleInfo* modules, int count) {
    if (!modules || count <= 0) {
        return COMPOSITOR_ERROR_INVALID_ARGS;
    }
    
    int i;
    for (i = 0; i < count; i++) {
        if (!modules[i].init) {
            log_message(COMPOSITOR_LOG_ERROR, "Module %s has no init function", modules[i].name);
            continue;
        }
        
        int ret = modules[i].init();
        if (ret != COMPOSITOR_OK) {
            log_message(COMPOSITOR_LOG_ERROR, "Failed to initialize module %s: %s", 
                       modules[i].name, compositor_error_to_string(ret));
            
            // 清理已初始化的模块
            compositor_cleanup_modules(modules, i, i);
            return ret;
        }
        
        log_message(COMPOSITOR_LOG_DEBUG, "Initialized module: %s", modules[i].name);
    }
    
    return COMPOSITOR_OK;
}

// 批量清理模块
void compositor_cleanup_modules(const ModuleInfo* modules, int count, int failed_index) {
    if (!modules || count <= 0) {
        return;
    }
    
    // 确定清理范围
    int end_index = (failed_index >= 0 && failed_index < count) ? failed_index : count;
    
    // 按相反顺序清理模块
    for (int i = end_index - 1; i >= 0; i--) {
        if (modules[i].cleanup) {
            modules[i].cleanup();
            log_message(COMPOSITOR_LOG_DEBUG, "Cleaned up module: %s", modules[i].name);
        }
    }
}

// 参数验证辅助函数
bool compositor_validate_pointer(const void* ptr, const char* name) {
    if (!ptr) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameter: %s is NULL", name ? name : "unknown");
        return false;
    }
    return true;
}

bool compositor_validate_string(const char* str, const char* name) {
    if (!str || strlen(str) == 0) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameter: %s is NULL or empty", name ? name : "unknown");
        return false;
    }
    return true;
}

bool compositor_validate_range(int value, int min, int max, const char* name) {
    if (value < min || value > max) {
        log_message(COMPOSITOR_LOG_ERROR, "Invalid parameter: %s=%d is out of range [%d, %d]", 
                   name ? name : "unknown", value, min, max);
        return false;
    }
    return true;
}

// 错误处理辅助函数
int compositor_handle_error(int error, const char* operation, const char* module) {
    if (error != COMPOSITOR_OK) {
        log_message(COMPOSITOR_LOG_ERROR, "Error in %s.%s: %s", 
                   module ? module : "unknown", 
                   operation ? operation : "unknown",
                   compositor_error_to_string(error));
    }
    return error;
}

// 性能测量辅助函数
void compositor_timer_start(PerformanceTimer* timer, const char* name) {
    if (!timer) return;
    
    timer->name = name ? name : "unnamed";
    timer->start_time = compositor_get_current_time_us();
    timer->end_time = 0;
}

uint64_t compositor_timer_end(PerformanceTimer* timer) {
    if (!timer) return 0;
    
    timer->end_time = compositor_get_current_time_us();
    uint64_t duration = timer->end_time - timer->start_time;
    
    if (duration > 1000) { // 只记录超过1ms的操作
        log_message(COMPOSITOR_LOG_DEBUG, "Timer %s: %llu us", 
                   timer->name ? timer->name : "unnamed", 
                   (unsigned long long)duration);
    }
    
    return duration;
}