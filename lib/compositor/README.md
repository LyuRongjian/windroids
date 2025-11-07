# Compositor Library

这是一个为Android GameActivity优化的高性能wlroots headless后端合成器库，支持Xwayland和Vulkan渲染。

## 功能特性

- 基于wlroots的headless后端
- 集成Xwayland支持
- Vulkan硬件加速渲染
- 简化的API接口，专为GameActivity设计
- 高性能，最小化资源使用
- 模块化设计，便于维护和扩展
- **性能优化功能**：自适应帧率/质量控制、热节流管理
- **游戏模式支持**：游戏特定优化、输入延迟优化
- **监控分析功能**：性能数据收集、报告生成
- **配置管理系统**：灵活的配置选项和持久化

## 使用方法

### 基本使用

1. 在GameActivity的onCreate中初始化合成器：

```c
#include "compositor.h"
#include "compositor_config.h"

int32_t width = ANativeWindow_getWidth(window);
int32_t height = ANativeWindow_getHeight(window);

// 使用默认配置初始化
if (compositor_init(window, width, height) != 0) {
    // 处理初始化失败
}

// 或者使用自定义配置
struct compositor_config config = default_compositor_config;
config.adaptive_framerate_enabled = true;
config.game_mode_enabled = true;
if (compositor_init_with_config(window, width, height, &config) != 0) {
    // 处理初始化失败
}
```

2. 在渲染循环中调用compositor_step：

```c
while (running) {
    if (compositor_step() != 0) {
        // 处理错误
    }
}
```

3. 注入输入事件：

```c
// 触摸事件
compositor_handle_input(0, x, y, 0, state); // state: 1=按下, 0=释放

// 键盘事件
compositor_handle_input(1, 0, 0, keycode, state); // state: 1=按下, 0=释放
```

4. 在GameActivity销毁时清理资源：

```c
compositor_destroy();
```

### 高级功能使用

```c
// 启用性能优化
compositor_perf_opt_enable();
compositor_perf_opt_set_adaptive_framerate(true);
compositor_perf_opt_set_adaptive_quality(true);
compositor_perf_opt_set_thermal_throttling(true);

// 启用游戏模式
compositor_game_mode_enable(GAME_MODE_PERFORMANCE);
compositor_game_set_input_optimization(true);
compositor_game_set_priority_boost(true);

// 启用监控
compositor_monitor_enable(1000); // 每秒采样一次
compositor_monitor_generate_report("/data/local/tmp/performance_report.txt");

// 配置管理
struct compositor_config config;
compositor_config_load("/data/local/tmp/compositor.conf", &config);
config.window_animations_enabled = false; // 禁用窗口动画
config.window_effects_enabled = false;    // 禁用窗口特效
compositor_config_save("/data/local/tmp/compositor.conf", &config);
```

## 构建依赖

- wlroots (headless后端)
- Vulkan
- Wayland协议库
- Android NDK

## 性能优化

本库专为性能优化，移除了以下非必要功能：

- 美观相关的UI效果和窗口动画
- 复杂的窗口管理功能（简化为基本窗口类型）
- 详细的性能统计和日志（可选启用）
- 多线程支持（单线程模型）
- 高级输入处理（仅保留基本鼠标事件）
- 窗口特效和过渡动画
- 复杂的Z-order管理（简化为基本层级）

## 模块结构

本库采用模块化设计，每个模块负责特定功能：

### 核心模块

- **compositor.c/h**: 主合成器模块，提供API接口和模块协调
- **compositor_input.c/h**: 输入事件处理模块
- **compositor_window.c/h**: 简化的窗口管理模块
- **compositor_render.c/h**: 优化的渲染模块
- **compositor_resource.c/h**: 资源管理模块
- **compositor_vulkan.c/h**: Vulkan渲染模块

### 优化模块

- **compositor_perf_opt.c/h**: 性能优化模块（自适应帧率/质量控制、热节流）
- **compositor_game.c/h**: 游戏模式模块（游戏特定优化、输入延迟优化）
- **compositor_monitor.c/h**: 监控分析模块（性能数据收集、报告生成）
- **compositor_config.c/h**: 配置管理模块（配置加载/保存、默认配置）

### 模块调用关系

```
compositor.c (主模块)
├── compositor_input.c (输入处理)
├── compositor_window.c (窗口管理)
├── compositor_render.c (渲染优化)
├── compositor_resource.c (资源管理)
├── compositor_vulkan.c (Vulkan渲染)
├── compositor_perf_opt.c (性能优化)
├── compositor_game.c (游戏模式)
├── compositor_monitor.c (监控分析)
└── compositor_config.c (配置管理)
```

### 模块职责

1. **compositor.c**: 初始化各模块，协调模块间交互，提供外部API
2. **compositor_input.c**: 处理触摸、鼠标、键盘等输入事件
3. **compositor_window.c**: 管理窗口创建、销毁、属性设置和焦点管理（简化版）
4. **compositor_render.c**: 优化渲染流程，管理渲染层和脏区域（无特效）
5. **compositor_resource.c**: 管理资源加载、卸载和内存使用
6. **compositor_vulkan.c**: 封装Vulkan API，提供渲染接口
7. **compositor_perf_opt.c**: 提供自适应帧率/质量控制、热节流管理
8. **compositor_game.c**: 提供游戏模式、输入优化、优先级提升
9. **compositor_monitor.c**: 提供性能监控、数据收集、报告生成
10. **compositor_config.c**: 提供配置管理、默认配置、持久化存储

### 代码限制

- 单个文件有效代码行数不超过1000行
- 模块间依赖清晰，接口明确
- 避免循环依赖

## 性能优化功能

### 自适应帧率控制
- 根据系统负载动态调整帧率
- 在性能不足时降低帧率，在性能充足时提高帧率
- 可配置目标帧率和调整策略

### 自适应质量控制
- 根据性能情况动态调整渲染质量
- 在性能不足时降低渲染质量，在性能充足时提高渲染质量
- 可配置质量级别和调整策略

### 热节流管理
- 监控系统温度，防止过热
- 在温度过高时降低性能，保护硬件
- 可配置温度阈值和节流策略

### 脏区域优化
- 只重绘屏幕上发生变化的区域
- 减少不必要的渲染，提高性能
- 自动合并相邻脏区域

## 游戏模式功能

### 游戏模式支持
- 为游戏应用优化资源分配
- 提供多种游戏模式（性能模式、平衡模式、质量模式）
- 可根据应用类型自动选择合适的模式

### 输入优化
- 减少输入延迟，提高响应速度
- 优化触摸事件处理路径
- 提供输入预测和插值

### 优先级提升
- 提高游戏应用的系统优先级
- 确保游戏获得更多CPU和GPU资源
- 可配置优先级级别

## 监控分析功能

### 性能数据收集
- 收集CPU、GPU、内存使用情况
- 记录帧率、帧时间等渲染指标
- 监控温度和功耗

### 报告生成
- 生成详细的性能分析报告
- 支持多种输出格式（文本、JSON、CSV）
- 可配置报告内容和频率

### 实时监控
- 提供实时性能数据
- 支持性能阈值告警
- 可通过API查询当前性能状态

## 配置管理

### 配置系统
- 提供完整的配置管理功能
- 支持配置文件加载和保存
- 提供默认配置和配置重置

### 配置选项
- 性能优化相关配置
- 游戏模式相关配置
- 监控分析相关配置
- 渲染和窗口管理配置

## 注意事项

- 本库设计为在GameActivity的渲染线程中运行
- 不支持多线程访问
- 简化的输入处理，仅支持基本的鼠标和键盘事件
- 各模块按需初始化，错误处理完善
- 默认禁用窗口动画和特效，提高性能
- 所有优化功能都有开关控制，可根据需要启用

## 开发优先级

1. **性能优化功能**（已完成）：
   - 自适应帧率控制
   - 自适应质量控制
   - 热节流管理
   - 脏区域优化

2. **游戏特定功能**（已完成）：
   - 游戏模式支持
   - 输入优化
   - 优先级提升

3. **监控和分析功能**（已完成）：
   - 性能数据收集
   - 报告生成
   - 实时监控

## 性能考虑

- 所有新功能都有开关控制，默认关闭
- 使用条件编译确保调试代码不影响发布版本性能
- 实现性能预算系统，确保总资源使用不超过限制
- 窗口动画和特效默认禁用，可根据需要启用
- 简化Z-order管理，减少计算开销

## Android集成

- 确保与Android生命周期正确集成
- 处理配置更改和内存不足情况
- 优化与Android系统的交互，减少开销
- 提供Android特定的性能优化选项
- 支持Android电源管理和热管理API