#!/usr/bin/env bash

# 构建compositor库的脚本
# 基于原Android.mk文件转换而来
# 所有环境变量均由外部传入，脚本内部不设置

# 参数处理
OUTPUT_DIR="$1"
ANDROID_NDK="$2"

if [ -z "$OUTPUT_DIR" ] || [ -z "$ANDROID_NDK" ]; then
    echo "Usage: $0 <output_dir> <android_ndk_path>"
    exit 1
fi

# 检查必要的环境变量
if [ -z "$CC" ]; then
    echo "Error: CC environment variable not set"
    exit 1
fi

if [ -z "$CXX" ]; then
    echo "Error: CXX environment variable not set"
    exit 1
fi

# 使用外部传入的CFLAGS和LDFLAGS，如果没有则使用默认值
CFLAGS="${CFLAGS:-}"
LDFLAGS="${LDFLAGS:-}"

# 源文件列表
SRC_FILES=(
    "compositor.c"
    "compositor_input.c"
    "compositor_window.c"
    "compositor_render.c"
    "compositor_perf.c"
    "compositor_perf_opt.c"
    "compositor_game.c"
    "compositor_monitor.c"
    "compositor_vulkan.c"
    "compositor_garbage_collector.c"
    "compositor_memory_pool.c"
    "compositor_resource_manager.c"
)

# 包含目录
INCLUDE_DIRS=(
    "."
    "$OUTPUT_DIR/include"
    "$OUTPUT_DIR/include/wlroots"
    "$OUTPUT_DIR/include/wayland"
    "$OUTPUT_DIR/include/vulkan"
)

# 编译标志
CFLAGS=(
    -Wall -Werror -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -DWLR_USE_UNSTABLE
    -std=c99
    -fPIC
    -O2
)

# 链接库
LIBS=(
    "-lwayland-server"
    "-lwlroots"
    "-lvulkan"
    "-llog"
    "-landroid"
)

# 创建输出目录
mkdir -p "$OUTPUT_DIR/lib"
mkdir -p "$OUTPUT_DIR/include/compositor"

# 构建包含路径参数
INCLUDE_ARGS=""
for dir in "${INCLUDE_DIRS[@]}"; do
    INCLUDE_ARGS="$INCLUDE_ARGS -I$dir"
done

# 编译目标文件
echo "Building compositor library..."
OBJ_FILES=()
for src in "${SRC_FILES[@]}"; do
    if [ -f "$src" ]; then
        obj="${src%.c}.o"
        echo "Compiling $src -> $obj"
        $CC $INCLUDE_ARGS "${CFLAGS[@]}" -c "$src" -o "$obj" || {
            echo "Failed to compile $src"
            exit 1
        }
        OBJ_FILES+=("$obj")
    else
        echo "Warning: Source file $src not found, skipping"
    fi
done

# 创建静态库
echo "Creating static library..."
ar rcs "$OUTPUT_DIR/lib/libcompositor.a" "${OBJ_FILES[@]}" || {
    echo "Failed to create static library"
    exit 1
}

# 创建共享库
echo "Creating shared library..."
$CC -shared -o "$OUTPUT_DIR/lib/libcompositor.so" "${OBJ_FILES[@]}" "${LIBS[@]}" $LDFLAGS -L"$OUTPUT_DIR/lib" || {
    echo "Failed to create shared library"
    exit 1
}

# 复制头文件
echo "Installing headers..."
cp *.h "$OUTPUT_DIR/include/compositor/" 2>/dev/null || true

# 清理目标文件
rm -f "${OBJ_FILES[@]}"

echo "Compositor library built successfully!"
echo "Static library: $OUTPUT_DIR/lib/libcompositor.a"
echo "Shared library: $OUTPUT_DIR/lib/libcompositor.so"