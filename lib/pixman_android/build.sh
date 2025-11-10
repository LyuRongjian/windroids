#!/usr/bin/env bash
# Build script for pixman_android (NEON-optimized)

set -euo pipefail

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MARKER="$SCRIPT_DIR/.built"

# 日志函数
log() {
    echo "[$(date +'%H:%M:%S')] $*"
}

# 主构建函数
build_pixman_android() {
    local output_dir="${1:-}"
    local ndk_path="${2:-}"
    
    if [ -z "$output_dir" ]; then
        log "❌ Usage: $0 <output_dir> [ndk_path]"
        return 1
    fi
    
    if [ -f "$MARKER" ]; then
        log "⏭️  pixman_android already built, skipping"
        return 0
    fi

    log "🔧 Building custom pixman_android with NDK compiler"
    log "ℹ️  Source: $SCRIPT_DIR"
    log "ℹ️  Output: $output_dir"
    
    # 验证 NDK 路径
    if [ -z "$ndk_path" ] || [ ! -d "$ndk_path" ]; then
        log "❌ Invalid NDK path: $ndk_path"
        return 1
    fi
    
    if [ ! -f "$ndk_path/build/cmake/android.toolchain.cmake" ]; then
        log "❌ Invalid NDK: missing android.toolchain.cmake"
        return 1
    fi
    
    export ANDROID_NDK="$ndk_path"
    log "ℹ️  Using NDK: $ANDROID_NDK"
    
    # 设置工具链
    local ndk_toolchain="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64"
    local cc="$ndk_toolchain/bin/aarch64-linux-android21-clang"
    local ar="$ndk_toolchain/bin/llvm-ar"
    
    if [ ! -f "$cc" ]; then
        log "❌ NDK compiler not found: $cc"
        return 1
    fi
    
    log "ℹ️  Compiler: $cc"
    log "ℹ️  Archiver: $ar"
    
    # 清理并创建构建目录
    rm -rf "$SCRIPT_DIR/build"
    mkdir -p "$SCRIPT_DIR/build"
    cd "$SCRIPT_DIR/build" || return 1

    log "🔧 Compiling pixman_android.c (Optimized + LTO)"
    "$cc" -c ../pixman_android.c \
        -o pixman_android.o \
        -I.. \
        -D__ANDROID__ -DANDROID \
        -fPIC -march=armv8-a+simd+crc+crypto -mtune=cortex-a76 -O3 \
        -fvectorize \
        -fslp-vectorize \
        -fmerge-all-constants \
        -funroll-loops \
        -finline-functions \
        -ffast-math \
        -fno-math-errno \
        -ffp-contract=fast \
        -flto=thin || {
        log "❌ Failed to compile pixman_android.c"
        return 1
    }
    
    # 检查cpufeatures路径
    local cpu_features_path="$ANDROID_NDK/sources/android/cpufeatures"
    if [ ! -f "$cpu_features_path/cpu-features.c" ]; then
        # 尝试新路径
        cpu_features_path="$ANDROID_NDK/sources/cpufeatures"
        if [ ! -f "$cpu_features_path/cpu-features.c" ]; then
            log "❌ Cannot find cpu-features.c in NDK"
            return 1
        fi
    fi
    
    log "🔧 Compiling cpu-features.c from $cpu_features_path"
    "$cc" -c "$cpu_features_path/cpu-features.c" \
        -o cpu-features.o \
        -I"$cpu_features_path" \
        -fPIC -O3 || {
        log "❌ Failed to compile cpu-features.c"
        return 1
    }
    
    log "🔧 Creating static library"
    "$ar" rcs libpixman-1.a pixman_android.o cpu-features.o || {
        log "❌ Failed to create libpixman-1.a"
        return 1
    }
    
    log "🔧 Installing to $output_dir"
    mkdir -p "$output_dir/lib" "$output_dir/include/pixman-1"
    cp libpixman-1.a "$output_dir/lib/" || return 1
    cp ../pixman.h "$output_dir/include/pixman-1/" || return 1
    
    log "🔧 Generating pkg-config file"
    mkdir -p "$output_dir/lib/pkgconfig"
    cat > "$output_dir/lib/pkgconfig/pixman-1.pc" <<EOF
prefix=$output_dir
libdir=\${prefix}/lib
includedir=\${prefix}/include/pixman-1

Name: Pixman
Description: The pixman library (NDK optimized with NEON)
Version: 0.42.2
Libs: -L\${libdir} -lpixman-1
Cflags: -I\${includedir}
EOF
    
    touch "$MARKER"
    log "✅ pixman_android built successfully"
}

# 如果直接执行脚本
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    build_pixman_android "$@"
fi
