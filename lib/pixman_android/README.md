# Pixman Android - 优化版本

这是针对Android和嵌入式系统优化的pixman库版本，专注于提供高性能的区域操作，同时确保对Wayland等关键组件的支持。

## 特性

- **高性能**: 使用O(1)复杂度的边界框计算，大幅提升性能
- **混合精度**: 支持简化模式和精确模式，可根据场景选择
- **Wayland兼容**: 专门优化以确保Wayland核心功能正常工作
- **NEON优化**: 在ARM平台上使用NEON指令集加速
- **API兼容**: 与标准pixman API完全兼容

## 使用方法

### 基本使用

```c
#include "pixman_android.h"

// 创建区域
pixman_region32_t region;
pixman_region32_init_rect(&region, 10, 10, 100, 100);

// 区域操作
pixman_region32_intersect(&dest, &region1, &region2);
pixman_region32_union(&dest, &region1, &region2);
pixman_region32_subtract(&dest, &region1, &region2);

// 点包含判断
if (pixman_region32_contains_point(&region, x, y, NULL)) {
    // 点在区域内
}

// 清理
pixman_region32_fini(&region);
```

### 精度模式切换

```c
// 设置为简化模式（默认，性能优先）
pixman_android_set_precise_mode(0);

// 设置为精确模式（功能优先）
pixman_android_set_precise_mode(1);

// 获取当前模式
int mode = pixman_android_get_precise_mode();
```

### Wayland集成建议

对于Wayland集成，建议在关键场景使用精确模式：

```c
// 在初始化时设置精确模式
pixman_android_set_precise_mode(1);

// 或者仅在特定操作中使用精确版本
pixman_region32_intersect_precise(&dest, &region1, &region2);
pixman_region32_contains_point_precise(&region, x, y, NULL);
```

## 性能优化

本实现通过以下方式提供高性能：

1. **边界框计算**: 使用O(1)复杂度的边界框计算，而不是O(n)的复杂区域计算
2. **内存效率**: 避免复杂的内存分配和释放
3. **NEON优化**: 在ARM平台上使用NEON指令集加速像素操作
4. **缓存友好**: 优化内存访问模式，提高缓存命中率

## 限制

1. **复杂形状**: 对于非矩形复杂形状，使用边界框近似
2. **精确减法**: 区域减法操作使用近似值，可能不是精确结果
3. **多边形**: 不支持复杂多边形操作

## 编译和测试

```bash
# 编译测试程序
make

# 运行测试
make test

# 清理构建文件
make clean

# 安装库和头文件
make install
```

## 与标准pixman的差异

| 功能 | 标准pixman | pixman_android | 说明 |
|------|------------|----------------|------|
| 区域交集 | O(n)复杂度 | O(1)复杂度 | 边界框近似 |
| 区域减法 | 精确结果 | 近似结果 | 边界框近似 |
| 点包含判断 | 精确 | 边界框检查 | 对大多数场景足够 |
| 内存使用 | 较高 | 较低 | 避免复杂分配 |

## 许可证

MIT许可证，与原始pixman项目兼容。

## 贡献

欢迎提交问题报告和改进建议。对于重大更改，请先创建issue讨论。