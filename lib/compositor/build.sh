#!/usr/bin/env bash
# Build script for compositor library

set -e

# 确保脚本在正确的目录中执行
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../" && pwd)"

# 加载环境变量
if [ -f "$PROJECT_ROOT/lib/env.sh" ]; then
    source "$PROJECT_ROOT/lib/env.sh"
fi

# 默认参数
OUTPUT_DIR="${1:-$OUTPUT_DIR}"
NDK_PATH="${2:-$ANDROID_NDK}"

if [ -z "$OUTPUT_DIR" ] || [ -z "$NDK_PATH" ]; then
    echo "Usage: $0 <output_dir> <ndk_path>"
    exit 1
fi

# 设置编译工具链
TARGET=aarch64-linux-android
API=29
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64"
CC="$TOOLCHAIN/bin/$TARGET$API-clang"
CXX="$TOOLCHAIN/bin/$TARGET$API-clang++"
AR="$TOOLCHAIN/bin/llvm-ar"
RANLIB="$TOOLCHAIN/bin/llvm-ranlib"

# 创建输出目录
mkdir -p "$OUTPUT_DIR/include"
mkdir -p "$OUTPUT_DIR/lib"

# 源文件和头文件
SRC_FILES=("$SCRIPT_DIR/compositor.c" \
           "$SCRIPT_DIR/vulkan/impl/compositor_vulkan.c" \
           "$SCRIPT_DIR/vulkan/core/compositor_vulkan_core.c" \
           "$SCRIPT_DIR/vulkan/init/compositor_vulkan_init.c" \
           "$SCRIPT_DIR/vulkan/render/compositor_vulkan_render.c" \
           "$SCRIPT_DIR/vulkan/render/compositor_vulkan_render_queue.c" \
           "$SCRIPT_DIR/vulkan/render/compositor_vulkan_window.c" \
           "$SCRIPT_DIR/vulkan/resource/compositor_vulkan_texture.c" \
           "$SCRIPT_DIR/vulkan/optimization/compositor_vulkan_optimization.c" \
           "$SCRIPT_DIR/vulkan/optimization/compositor_vulkan_adapt.c" \
           "$SCRIPT_DIR/vulkan/optimization/compositor_vulkan_perf.c" \
           "$SCRIPT_DIR/compositor_config.c" \
           "$SCRIPT_DIR/compositor_utils.c" \
           "$SCRIPT_DIR/compositor_input.c" \
           "$SCRIPT_DIR/compositor_window.c")

HDR_FILES=("$SCRIPT_DIR/compositor.h" \
           "$SCRIPT_DIR/vulkan/impl/compositor_vulkan.h" \
           "$SCRIPT_DIR/vulkan/core/compositor_vulkan_core.h" \
           "$SCRIPT_DIR/vulkan/init/compositor_vulkan_init.h" \
           "$SCRIPT_DIR/vulkan/render/compositor_vulkan_render.h" \
           "$SCRIPT_DIR/vulkan/render/compositor_vulkan_render_queue.h" \
           "$SCRIPT_DIR/vulkan/render/compositor_vulkan_window.h" \
           "$SCRIPT_DIR/vulkan/resource/compositor_vulkan_texture.h" \
           "$SCRIPT_DIR/vulkan/optimization/compositor_vulkan_optimization.h" \
           "$SCRIPT_DIR/vulkan/optimization/compositor_vulkan_adapt.h" \
           "$SCRIPT_DIR/vulkan/optimization/compositor_vulkan_perf.h" \
           "$SCRIPT_DIR/compositor_config.h" \
           "$SCRIPT_DIR/compositor_utils.h" \
           "$SCRIPT_DIR/compositor_input.h" \
           "$SCRIPT_DIR/compositor_window.h")

# 编译标志
CFLAGS="-I$SCRIPT_DIR -I$SCRIPT_DIR/vulkan/interface -I$SCRIPT_DIR/vulkan/core -I$SCRIPT_DIR/vulkan/init -I$SCRIPT_DIR/vulkan/render -I$SCRIPT_DIR/vulkan/resource -I$SCRIPT_DIR/vulkan/optimization -I$OUTPUT_DIR/include -I$NDK_PATH/sysroot/usr/include -I$NDK_PATH/sysroot/usr/include/$TARGET -fPIC -Wall -Wextra -O2 -std=c99"
LDFLAGS="-L$OUTPUT_DIR/lib -lvulkan -landroid -ldl -llog"

# 复制头文件到输出目录
echo "📂 Copying header files to $OUTPUT_DIR/include"
for hdr in "${HDR_FILES[@]}"; do
    cp "$hdr" "$OUTPUT_DIR/include/"
done

# 编译静态库
echo "🔧 Building static library libcompositor.a"
OBJ_FILES=()
for src in "${SRC_FILES[@]}"; do
    obj="${src%.c}.o"
    $CC -c $CFLAGS "$src" -o "$obj"
    OBJ_FILES+=("$obj")
done

$AR rcs "$OUTPUT_DIR/lib/libcompositor.a" "${OBJ_FILES[@]}"
$RANLIB "$OUTPUT_DIR/lib/libcompositor.a"

echo "✅ Build completed successfully!"
echo "📦 Library location: $OUTPUT_DIR/lib/libcompositor.a"

# 编译共享库
echo "🔧 Building shared library libcompositor.so"
$CC -shared $CFLAGS $SRC_FILE -o "$OUTPUT_DIR/lib/libcompositor.so" $LDFLAGS

# 复制头文件
echo "📄 Copying header file"
cp "$HDR_FILE" "$OUTPUT_DIR/include/"

# 清理临时文件
rm -f "$SCRIPT_DIR/compositor.o"

echo "✅ Compositor library built successfully"
echo "📁 Output directory: $OUTPUT_DIR"
echo "📚 Libraries: $OUTPUT_DIR/lib/libcompositor.a, $OUTPUT_DIR/lib/libcompositor.so"
echo "📄 Header: $OUTPUT_DIR/include/compositor.h"