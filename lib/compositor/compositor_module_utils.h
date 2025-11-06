#ifndef COMPOSITOR_MODULE_UTILS_H
#define COMPOSITOR_MODULE_UTILS_H

#include "compositor.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 模块初始化函数类型
typedef int (*ModuleInitFunc)(void);
typedef void (*ModuleCleanupFunc)(void);

// 模块初始化和清理的辅助结构
typedef struct {
    const char* name;
    ModuleInitFunc init;
    ModuleCleanupFunc cleanup;
} ModuleInfo;

// 批量初始化模块
int compositor_init_modules(const ModuleInfo* modules, int count);

// 批量清理模块
void compositor_cleanup_modules(const ModuleInfo* modules, int count, int failed_index);

// 模块初始化宏，用于简化模块定义
#define DEFINE_MODULE(name, init_func, cleanup_func) \
    { #name, init_func, cleanup_func }

// 参数验证辅助函数
bool compositor_validate_pointer(const void* ptr, const char* name);
bool compositor_validate_string(const char* str, const char* name);
bool compositor_validate_range(int value, int min, int max, const char* name);

// 错误处理辅助函数
int compositor_handle_error(int error, const char* operation, const char* module);

// 性能测量辅助函数
typedef struct {
    const char* name;
    uint64_t start_time;
    uint64_t end_time;
} PerformanceTimer;

void compositor_timer_start(PerformanceTimer* timer, const char* name);
uint64_t compositor_timer_end(PerformanceTimer* timer);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_MODULE_UTILS_H