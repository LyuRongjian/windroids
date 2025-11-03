# WinDroids Compositor Library

## 概述

这是一个基于 wlroots + Xwayland + Vulkan 的合成器库，专为 Android GameActivity 设计，提供了在 Android 平台上运行 Linux 和 Windows 图形应用的能力。

## 核心功能

- **初始化 Wayland compositor**：基于 wlroots headless 后端
- **启动 Xwayland 子进程**：支持运行 X11 应用
- **自定义 Vulkan 渲染器**：高效合成所有窗口
- **ANativeWindow 集成**：将最终帧输出到 Android 窗口
- **输入事件处理**：支持触摸、键盘等输入事件

## 架构设计

```
GameActivity (Android) ←→ compositor.h (C ABI) ←→ compositor.c (实现)
                                       ↓
                   ┌──────────────────┼──────────────────┐
                   ↓                  ↓                  ↓
            wlroots (headless)   Xwayland (子进程)  Vulkan 渲染器
                   ↓                  ↓                  ↓
                   └──────────────────┼──────────────────┘
                                      ↓
                               ANativeWindow (输出)
```

## 编译说明

### 依赖项

- Android NDK (API 29 或更高)
- Meson + Ninja (构建系统)
- wlroots 0.18.3
- Xwayland 21.1.13
- Vulkan SDK (运行时)

### 编译步骤

1. 确保已安装所有依赖
2. 运行主构建脚本：

```bash
cd /home/lrj/windroids
./windroids-build.sh
```

3. 构建产物将位于 `output/` 目录：
   - `output/lib/libcompositor.a` (静态库)
   - `output/lib/libcompositor.so` (共享库)
   - `output/include/compositor.h` (头文件)
   - `output/bin/xwayland` (Xwayland 可执行文件)

## API 使用示例

以下是在 GameActivity 的 native 代码中使用此库的示例：

```c
#include <compositor.h>
#include <android/native_activity.h>
#include <android/native_window_jni.h>

// 全局合成器引用
ANativeWindow* g_native_window = NULL;

// GameActivity 回调
static void on_window_init(ANativeActivity* activity) {
    g_native_window = activity->window;
    if (!g_native_window) {
        return;
    }
    
    // 获取窗口尺寸
    int width = ANativeWindow_getWidth(g_native_window);
    int height = ANativeWindow_getHeight(g_native_window);
    
    // 初始化合成器
    if (compositor_init(g_native_window, width, height) != 0) {
        // 处理错误
        return;
    }
}

// 渲染循环
static void render_frame() {
    // 执行合成器单步
    compositor_step();
}

// 触摸事件处理
static int32_t on_touch_event(ANativeActivity* activity, AInputEvent* event) {
    if (!g_native_window) {
        return 0;
    }
    
    int32_t action = AMotionEvent_getAction(event);
    int pointer_idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    float x = AMotionEvent_getX(event, pointer_idx);
    float y = AMotionEvent_getY(event, pointer_idx);
    int state = (action & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_DOWN ? 1 : 0;
    
    // 注入触摸事件到合成器
    compositor_handle_input(0, (int)x, (int)y, 0, state);
    
    return 1;
}

// 清理资源
static void on_destroy(ANativeActivity* activity) {
    compositor_destroy();
}

// 设置 GameActivity 回调
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    activity->callbacks->onWindowInit = on_window_init;
    activity->callbacks->onDestroy = on_destroy;
    activity->callbacks->onInputEvent = on_touch_event;
    
    // 启动渲染线程
    // ...
}
```

## 集成到 Android 项目

### 步骤 1: 复制库文件

将编译产物复制到 Android 项目的 `app/src/main/jniLibs/arm64-v8a/` 目录：

```
app/src/main/jniLibs/arm64-v8a/
├── libcompositor.so
└── xwayland
```

### 步骤 2: 配置 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.18.1)

project("windroids_example")

# 添加合成器库
add_library(compositor SHARED IMPORTED)
set_target_properties(compositor PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/src/main/jniLibs/arm64-v8a/libcompositor.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/src/main/cpp/include"
)

# 添加应用代码
add_library(native-lib SHARED
    src/main/cpp/native-lib.cpp
)

# 链接库
target_link_libraries(native-lib
    compositor
    android
    log
    vulkan
)
```

### 步骤 3: 配置 build.gradle

```groovy
android {
    // ...
    defaultConfig {
        // ...
        externalNativeBuild {
            cmake {
                arguments "-DANDROID_STL=c++_shared"
            }
        }
        ndk {
            abiFilters 'arm64-v8a'
        }
    }
    // ...
}
```

## 注意事项

1. **权限**：确保应用具有必要的权限，如 `INTERNET` 和 `READ_EXTERNAL_STORAGE`
2. **Xwayland 路径**：compositor.c 中的 Xwayland 路径需要根据实际安装位置修改
3. **线程安全**：所有合成器操作必须在 GameActivity 的渲染线程中执行
4. **性能优化**：可以根据需要调整 Vulkan 的配置，如呈现模式、图像格式等

## 故障排除

1. **Vulkan 初始化失败**：检查设备是否支持 Vulkan，以及是否正确加载了 libvulkan.so
2. **Xwayland 启动失败**：确保 Xwayland 可执行文件具有执行权限，并且路径正确
3. **窗口不显示**：检查 ANativeWindow 是否正确传递，以及尺寸设置是否合理
4. **性能问题**：考虑调整 Vulkan 的设置，如使用更高效的呈现模式或减少不必要的渲染操作

## 未来优化方向

1. 添加多窗口管理支持
2. 实现硬件加速的窗口合成
3. 支持更多输入设备类型
4. 添加 OpenGL ES 作为备选渲染后端
5. 优化内存使用和渲染性能


## 优化建议

### 1. 函数接口与声明优化

- **函数声明完整性**：已修复 `is_point_in_xwayland_surface` 函数的静态声明缺失问题，但其他静态函数如 `is_point_in_wayland_window`、`is_point_in_decoration` 等也应该在头文件中添加适当的静态声明，以提高代码的一致性。

- **函数参数一致性**：检查 `compositor_handle_input` 函数的文档注释，确保所有事件类型常量名称与实际代码使用的 `COMPOSITOR_INPUT_*` 格式一致。

### 2. Wayland 窗口功能实现

- **功能完善**：代码中有多处标记为 `TODO: 实现Wayland窗口XXX逻辑` 的注释，需要完成这些功能实现，包括：
  - Wayland窗口焦点设置
  - Wayland窗口激活逻辑
  - Wayland窗口关闭逻辑
  - Wayland窗口大小调整逻辑
  - Wayland窗口移动逻辑
  - Wayland窗口信息获取
  - Wayland窗口最小化/最大化/还原
  - Wayland窗口透明度设置

### 3. 窗口状态管理优化

- **状态保存**：当前窗口最小化和还原实现较为简单，使用了将窗口移出屏幕的方式。建议实现更完善的窗口状态管理，使用结构体存储窗口的原始位置和大小，以便更精确地还原窗口状态。

```c
// 建议添加窗口状态结构体
typedef struct {
    int saved_x, saved_y;
    int saved_width, saved_height;
    bool is_minimized;
    bool is_maximized;
    float opacity;
    // 其他窗口状态...
} WindowState;
```

### 4. 输入事件处理改进

- **事件转发逻辑**：实现所有标记为 `TODO: 实现具体的事件转发逻辑` 的部分，确保鼠标和键盘事件能够正确转发到相应的窗口。

- **鼠标光标管理**：添加鼠标悬停效果和光标形状变化的实现。

### 5. 错误处理增强

- **错误码定义**：虽然已添加 `COMPOSITOR_ERROR_INVALID_PARAMETER` 错误码，但建议为所有功能模块定义更具体的错误码，以提高错误诊断的准确性。

- **统一错误报告**：确保所有错误处理路径都设置了适当的错误代码和错误消息。

### 6. 代码可维护性提升

- **减少重复代码**：在窗口管理相关函数中存在大量重复逻辑，特别是在遍历窗口列表方面。建议提取公共函数，如：

```c
// 建议添加查找窗口的公共函数
struct wlr_xwayland_surface* find_xwayland_surface_by_title(const char* window_title) {
    struct wlr_xwayland_surface *surface;
    wl_list_for_each(surface, &compositor_state.xwayland_surfaces, link) {
        if (surface->title && strcmp(surface->title, window_title) == 0) {
            return surface;
        }
    }
    return NULL;
}
```

- **参数验证集中化**：在所有公共API函数开始处添加参数验证，并将常见的验证逻辑提取为辅助函数。

### 7. 性能优化

- **渲染优化**：进一步优化 `render_frame` 函数，确保只在必要时进行重绘。

- **链表操作优化**：在处理大量窗口时，优化窗口链表的搜索和排序操作，考虑使用更高效的数据结构。

### 8. 资源管理

- **内存泄漏检查**：确保在所有错误处理路径上都正确释放分配的内存。

- **引用计数**：考虑为窗口和表面添加引用计数机制，以确保资源不会被过早释放。

### 9. 功能拓展

- **窗口边缘吸附**：在窗口移动时实现边缘吸附功能，提升用户体验。

- **多显示器支持**：增强多显示器支持，确保窗口可以正确地在不同显示器之间移动和排列。

## 总结

compositor.c 和 compositor.h 文件总体质量良好，没有明显的语法错误。主要的优化方向是完成未实现的 Wayland 窗口功能、改进窗口状态管理、完善错误处理机制以及提高代码的可维护性和性能。这些优化将使代码更加健壮，功能更加完整，用户体验更加流畅。
        