#ifndef COMPOSITOR_INPUT_PERFORMANCE_H
#define COMPOSITOR_INPUT_PERFORMANCE_H

#include <stdint.h>
#include <stdbool.h>
#include "compositor_input.h"
#include "compositor_utils.h"

// 初始化输入性能统计模块
int compositor_input_performance_init(void);

// 清理输入性能统计模块
void compositor_input_performance_cleanup(void);

// 更新输入性能统计
void compositor_input_performance_update_stats(CompositorInputDeviceType device_type, int64_t response_time);

// 获取输入性能统计
int compositor_input_performance_get_stats(CompositorInputPerformanceStats* stats);

// 自适应调整输入处理参数
void compositor_input_performance_adapt_processing(void);

// 获取输入频率
int compositor_input_performance_get_frequency(void);

// 重置性能统计
void compositor_input_performance_reset_stats(void);

#endif // COMPOSITOR_INPUT_PERFORMANCE_H