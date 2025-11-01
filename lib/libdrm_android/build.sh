#!/usr/bin/env bash
# Build script for libdrm_android (DRM shim + xf86drm API)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MARKER="$SCRIPT_DIR/.built"

log() {
    echo "[$(date +'%H:%M:%S')] $*"
}

build_drm_shim() {
    local output_dir="${1:-}"
    local ndk_path="${2:-}"
    
    if [ -f "$MARKER" ]; then
        log "⏭️  libdrm_android already built"
        return 0
    fi
    
    if [ ! -d "$ndk_path" ]; then
        log "❌ Invalid NDK: $ndk_path"
        return 1
    fi
    
    export ANDROID_NDK="$ndk_path"
    local cc="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang"
    local ar="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar"
    
    log "🔧 Building DRM shim (NEON + LTO + Trace + ashmem)"
    
    cd "$SCRIPT_DIR" || return 1
    
    # 编译 drm_shim.c
    log "🔧 Compiling drm_shim.c (LTO + NEON + Size Optimization)"
    "$cc" -c -fPIC -O3 \
        -march=armv8-a+simd+crc+crypto \
        -mtune=cortex-a76 \
        -moutline-atomics \
        -DANDROID -D__ANDROID__ -pthread \
        -Wall -Wextra \
        -ffast-math \
        -fno-strict-aliasing \
        -fomit-frame-pointer \
        -funroll-loops \
        -finline-functions \
        -fvisibility=hidden \
        -flto=thin \
        -fvectorize \
        -fslp-vectorize \
        -fno-exceptions \
        -fno-rtti \
        -fno-unwind-tables \
        -fno-asynchronous-unwind-tables \
        drm_shim.c -o drm_shim.o -I. || {
        log "❌ Failed to compile drm_shim.c"
        return 1
    }
    
    # 编译 xf86drm.c（添加 Atomic API）
    log "🔧 Compiling xf86drm.c (with Atomic Modesetting API)"
    "$cc" -c -fPIC -O3 \
        -march=armv8-a+simd+crc+crypto \
        -mtune=cortex-a76 \
        -moutline-atomics \
        -DANDROID -D__ANDROID__ -pthread \
        -Wall -Wextra -Wno-unused-parameter \
        -ffast-math \
        -fno-strict-aliasing \
        -fomit-frame-pointer \
        -funroll-loops \
        -finline-functions \
        -fvisibility=hidden \
        -flto=thin \
        -fvectorize \
        -fslp-vectorize \
        -fno-exceptions \
        -fno-rtti \
        -fno-unwind-tables \
        -fno-asynchronous-unwind-tables \
        -fmerge-all-constants \
        xf86drm.c -o xf86drm.o -I. || {
        log "❌ Failed to compile xf86drm.c"
        return 1
    }
    
    # 创建静态库
    log "🔧 Creating libdrm_shim.a"
    "$ar" rcs libdrm_shim.a drm_shim.o xf86drm.o || {
        log "❌ Failed to create static library"
        return 1
    }
    
    # 创建动态库（启用 LTO + 死代码删除）
    log "📦 Linking libdrm_shim.so (ICF + Dead code elimination)"
    "$cc" -shared -fPIC -O3 -march=armv8-a+simd -pthread \
        -flto=thin \
        -fuse-ld=lld \
        -Wl,--gc-sections \
        -Wl,--as-needed \
        -Wl,-z,relro \
        -Wl,-z,now \
        -Wl,-z,max-page-size=16384 \
        -Wl,--hash-style=gnu \
        -Wl,--sort-section=alignment \
        -Wl,--icf=all \
        -fdata-sections \
        -ffunction-sections \
        drm_shim.o xf86drm.o ${cpu_features_obj} \
        -o libdrm_shim.so \
        -landroid -llog -ldl || {
        log "❌ Failed to link libdrm_shim.so"
        return 1
    }
    
    # 安装
    log "🔧 Installing to $output_dir"
    mkdir -p "$output_dir/lib" "$output_dir/include/libdrm"
    cp libdrm_shim.a libdrm_shim.so "$output_dir/lib/" || return 1
    cp drm.h xf86drm.h "$output_dir/include/libdrm/" || return 1
    
    # 创建符号链接
    ln -sf libdrm/drm.h "$output_dir/include/drm.h" 2>/dev/null || true
    ln -sf libdrm/xf86drm.h "$output_dir/include/xf86drm.h" 2>/dev/null || true
    
    # 生成 pkg-config
    log "🔧 Generating pkg-config"
    mkdir -p "$output_dir/lib/pkgconfig"
    cat > "$output_dir/lib/pkgconfig/libdrm.pc" <<EOF
prefix=$output_dir
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libdrm
Description: Userspace interface to kernel DRM services (Android shim)
Version: 2.4.120
Libs: -L\${libdir} -ldrm_shim -landroid -llog
Cflags: -I\${includedir}/libdrm -I\${includedir}
EOF
    
    touch "$MARKER"
    log "✅ libdrm_android built successfully"
}

# 修复：只保留一个主入口
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    build_drm_shim "$@"
fi