#!/usr/bin/env bash
# Build helpers and main build_all

clean_build_logs() {
    local timestamp
    timestamp=$(date +%Y%m%d_%H%M%S)
    local archived=0
    # 归档旧日志（而不是删除），保留最近的构建记录
    for f in "$PROJECT_ROOT/build.log" "$SRC_DIR/build.log" "$OUTPUT_DIR/build.log"; do
        if [ -f "$f" ]; then
            mv "$f" "${f}.${timestamp}" 2>/dev/null && archived=$((archived+1))
        fi
    done
    if [ "$archived" -gt 0 ]; then
        log "🗃️  archived $archived old build.log file(s) with timestamp $timestamp"
    fi
}

build_meson() {
    local dir=$1
    shift
    local marker="$SRC_DIR/$dir/.built"
    if [ -f "$marker" ]; then
        log "⏭️  $dir already built (found $marker), skipping"
        return 0
    fi
    log "🔧 meson build: $dir"
    pushd "$SRC_DIR/$dir" >/dev/null

    # 为 wayland 阶段追加 gnu99，避免 libffi 子项目使用 asm 报错
    if [ "$dir" = "wayland" ]; then
        export CFLAGS="${CFLAGS:-} -std=gnu99"
    fi

    # ==== pixman 特殊处理（针对 NDK/Android） ====
    # 在 NDK 下编译 pixman 易遇到：缺少 Android 宏、PIC 要求、默认生成 shared 导致链接问题、测试/文档依赖等。
    # 这里为 pixman 注入安全的 CFLAGS/LDFLAGS 与 meson 选项以提高通过率。
    local MESON_EXTRA_ARGS=()
    if [ "$dir" = "pixman" ]; then
        export CFLAGS="${CFLAGS:-} -D__ANDROID__ -DANDROID -fPIC -march=armv8-a -O3"
        export LDFLAGS="${LDFLAGS:-} -L$OUTPUT_DIR/lib -Wl,--exclude-libs,ALL"
        # 禁用汇编以规避 NDK 宏兼容问题（上游 pixman 的 ARM asm 依赖 GNU AS 宏）
        MESON_EXTRA_ARGS+=( -Ddefault_library=static -Dtests=disabled -Dlibpng=disabled -Dasm=disabled )
        log "🔧 pixman: applying Android-specific CFLAGS/LDFLAGS and meson args (asm=disabled)"
    fi

    # 移除：不再尝试预编译 subprojects/libffi，交给 wrap fallback 处理

    # 优先让 pkg-config 查找 subprojects/libffi/build 下的 libffi.pc
    local ffi_pc_dir=""
    if [ -f "subprojects/libffi/build/libffi.pc" ]; then
        ffi_pc_dir="$PWD/subprojects/libffi/build"
    elif [ -f "$COMMON_SUBPROJECTS/libffi/build/libffi.pc" ]; then
        ffi_pc_dir="$COMMON_SUBPROJECTS/libffi/build"
    fi

    if [ -n "$ffi_pc_dir" ]; then
        export PKG_CONFIG_PATH="$ffi_pc_dir:$HOST_TOOLS_DIR/lib/pkgconfig:$OUTPUT_DIR/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
        export PKG_CONFIG_LIBDIR="$ffi_pc_dir:$HOST_TOOLS_DIR/lib/pkgconfig:$OUTPUT_DIR/lib/pkgconfig:${PKG_CONFIG_LIBDIR:-}"
        log "🔧 PKG_CONFIG_PATH for $dir: $PKG_CONFIG_PATH"
        log "🔧 PKG_CONFIG_LIBDIR for $dir: $PKG_CONFIG_LIBDIR"
    else
        export PKG_CONFIG_PATH="$HOST_TOOLS_DIR/lib/pkgconfig:$OUTPUT_DIR/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
        export PKG_CONFIG_LIBDIR="$HOST_TOOLS_DIR/lib/pkgconfig:$OUTPUT_DIR/lib/pkgconfig:${PKG_CONFIG_LIBDIR:-}"
    fi

    export CFLAGS="${CFLAGS:-} -I$OUTPUT_DIR/include"
    export LDFLAGS="${LDFLAGS:-} -L$OUTPUT_DIR/lib"

    if [ -d build ]; then
        if [ "$dir" = "pixman" ]; then
            meson setup --reconfigure build --cross-file "$PROJECT_ROOT/android-cross.txt" --native-file "$PROJECT_ROOT/native.ini" --prefix "$OUTPUT_DIR" --libdir=lib "$@" "${MESON_EXTRA_ARGS[@]}" >"$PROJECT_ROOT/pixman-meson-setup.log" 2>&1 || {
                log "❌ meson setup for pixman failed — see $PROJECT_ROOT/pixman-meson-setup.log"
                log "ℹ️ Common fixes: ensure cross-file toolchain is correct, disable optional deps (libpng), ensure pkg-config finds libffi, and inspect the meson log above."
                log "ℹ️ To debug: tail -n 200 $PROJECT_ROOT/pixman-meson-setup.log"
                # print short excerpt to help interactive debugging
                echo "---- meson setup log excerpt (last 100 lines) ----"
                tail -n 100 "$PROJECT_ROOT/pixman-meson-setup.log" | sed -n '1,100p'
                popd >/dev/null
                return 1
            }
        else
            meson setup --reconfigure build --cross-file "$PROJECT_ROOT/android-cross.txt" --native-file "$PROJECT_ROOT/native.ini" --prefix "$OUTPUT_DIR" --libdir=lib "$@" "${MESON_EXTRA_ARGS[@]}" || { popd >/dev/null; return 1; }
        fi
    else
        if [ "$dir" = "pixman" ]; then
            meson setup build --cross-file "$PROJECT_ROOT/android-cross.txt" --native-file "$PROJECT_ROOT/native.ini" --prefix "$OUTPUT_DIR" --libdir=lib "$@" "${MESON_EXTRA_ARGS[@]}" >"$PROJECT_ROOT/pixman-meson-setup.log" 2>&1 || {
                log "❌ meson setup for pixman failed — see $PROJECT_ROOT/pixman-meson-setup.log"
                log "ℹ️ Common fixes: ensure cross-file toolchain is correct, disable optional deps (libpng), ensure pkg-config finds libffi, and inspect the meson log above."
                echo "---- meson setup log excerpt (last 100 lines) ----"
                tail -n 100 "$PROJECT_ROOT/pixman-meson-setup.log" | sed -n '1,100p'
                popd >/dev/null
                return 1
            }
        else
            meson setup build --cross-file "$PROJECT_ROOT/android-cross.txt" --native-file "$PROJECT_ROOT/native.ini" --prefix "$OUTPUT_DIR" --libdir=lib "$@" "${MESON_EXTRA_ARGS[@]}" || { popd >/dev/null; return 1; }
        fi
    fi
    if [ "$dir" = "pixman" ]; then
        ninja -C build >"$PROJECT_ROOT/pixman-build.log" 2>&1 || {
            log "❌ ninja build for pixman failed — see $PROJECT_ROOT/pixman-build.log"
            echo "---- pixman ninja build log excerpt (last 200 lines) ----"
            tail -n 200 "$PROJECT_ROOT/pixman-build.log" | sed -n '1,200p'
            popd >/dev/null
            return 1
        }
        ninja -C build install >>"$PROJECT_ROOT/pixman-build.log" 2>&1 || {
            log "❌ ninja install for pixman failed — see $PROJECT_ROOT/pixman-build.log"
            tail -n 200 "$PROJECT_ROOT/pixman-build.log" | sed -n '1,200p'
            popd >/dev/null
            return 1
        }
    else
        ninja -C build || { popd >/dev/null; return 1; }
        ninja -C build install || { popd >/dev/null; return 1; }
    fi
    touch "$marker"
    log "✅ $dir built successfully"
    popd >/dev/null
}

build_autotools() {
    local dir=$1
    shift
    local marker="$SRC_DIR/$dir/.built"
    if [ -f "$marker" ]; then
        log "⏭️  $dir already built (found $marker), skipping"
        return 0
    fi
    log "🔧 autotools build: $dir"
    pushd "$SRC_DIR/$dir" >/dev/null
    if [ -f autogen.sh ] && [ ! -f configure ]; then
        ./autogen.sh --no-configure || true
    fi
    ./configure --host=aarch64-linux-android --prefix="$OUTPUT_DIR" --disable-shared --enable-static "$@" || { popd >/dev/null; return 1; }
    make -j"$(nproc)" || { popd >/dev/null; return 1; }
    make install || { popd >/dev/null; return 1; }
    touch "$marker"
    log "✅ $dir built successfully"
    popd >/dev/null
}

build_xwayland() {
    local marker="$SRC_DIR/xorg_server/.built"
    if [ -f "$marker" ]; then
        log "⏭️  xorg_server (xwayland) already built, skipping"
        return 0
    fi
    pushd "$SRC_DIR/xorg_server" >/dev/null
    rm -f configure || true
    if [ -f autogen.sh ]; then
        ./autogen.sh --host=aarch64-linux-android --enable-xwayland --disable-xorg --disable-dri --disable-glamor --prefix="$OUTPUT_DIR" CFLAGS="-I$OUTPUT_DIR/include" LDFLAGS="-L$OUTPUT_DIR/lib" || { popd >/dev/null; return 1; }
    fi
    ./configure --host=aarch64-linux-android --prefix="$OUTPUT_DIR" --enable-xwayland --disable-xorg --disable-dri --disable-glamor CFLAGS="-I$OUTPUT_DIR/include" LDFLAGS="-L$OUTPUT_DIR/lib" || { popd >/dev/null; return 1; }
    make -j"$(nproc)" || { popd >/dev/null; return 1; }
    make install || { popd >/dev/null; return 1; }
    mkdir -p "$OUTPUT_DIR/bin"
    cp hw/xwayland/xwayland "$OUTPUT_DIR/bin/xwayland" || true
    touch "$marker"
    log "✅ xorg_server (xwayland) built successfully"
    popd >/dev/null
}

build_pixman_android() {
    # 从 android-cross.txt 提取 NDK 路径
    local ndk_path=""
    if [ -f "$PROJECT_ROOT/android-cross.txt" ]; then
        local cc_path=$(grep "^c = " "$PROJECT_ROOT/android-cross.txt" | cut -d"'" -f2)
        if [ -n "$cc_path" ]; then
            ndk_path=$(dirname $(dirname $(dirname $(dirname $(dirname $(dirname "$cc_path"))))))
        fi
    fi
    
    # Fallback
    if [ -z "$ndk_path" ] || [ ! -d "$ndk_path" ]; then
        ndk_path="${PROJECT_ROOT}/ndk"
    fi
    
    if [ ! -d "$ndk_path" ]; then
        log "❌ Cannot find NDK"
        return 1
    fi
    
    export ANDROID_NDK="$ndk_path"
    
    # 调用子脚本
    bash "$PROJECT_ROOT/../lib/pixman_android/build.sh" "$OUTPUT_DIR" "$ndk_path" || {
        log "❌ Failed to build pixman_android"
        return 1
    }
}

# DRM dependencies are removed as per requirements

build_wlroots() {
    echo "Building wlroots without DRM dependencies..."
    # 设置环境变量
    export PKG_CONFIG_PATH="$OUTPUT_DIR/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    export CFLAGS="-I$OUTPUT_DIR/include ${CFLAGS:-}"
    export LDFLAGS="-L$OUTPUT_DIR/lib ${LDFLAGS:-}"
    
    # Ensure no DRM dependencies are used
    build_meson wlroots \
        -Dexamples=false \
        -Dbackends=headless \
        -Drenderers=pixman,gles2 \
        -Dxwayland=enabled \
        -Dlibseat=disabled \
        -Dudev=disabled \
        -Dsystemd=disabled \
        -Dlibffi:exe_static_tramp=true \
        -Dlinux-dmabuf=disabled
}

prepare_cross_deps() {
    local wrap_dir="$SRC_DIR/meson-wrap"
    mkdir -p "$wrap_dir"
    pushd "$wrap_dir" >/dev/null

    mkdir -p subprojects

    # 安全读取 wrap 字段
    _wrap_field() {
        sed -n "s/^$1[[:space:]]*=[[:space:]]*//p" "$2" | head -n1
    }

    local all_ok=1
    for dep in libffi expat libxml2; do
        meson wrap install "$dep" || true

        local wrap_file="subprojects/${dep}.wrap"
        if [ ! -f "$wrap_file" ]; then
            log "❌ wrap file for $dep not found in $wrap_dir/subprojects/"
            all_ok=0
            continue
        fi

        # 字段
        local source_url source_hash source_fallback_url proj_dir
        source_url=$(_wrap_field "source_url" "$wrap_file")
        source_hash=$(_wrap_field "source_hash" "$wrap_file")
        source_fallback_url=$(_wrap_field "source_fallback_url" "$wrap_file")
        proj_dir=$(_wrap_field "directory" "$wrap_file")
        if [ -z "$proj_dir" ]; then
            local fname; fname="$(basename "$source_url")"
            proj_dir="${fname%%.tar.*}"; proj_dir="${proj_dir%%.tgz}"; proj_dir="${proj_dir%%.zip}"
            [ -z "$proj_dir" ] && proj_dir="$dep"
        fi

        local tarball="$wrap_dir/$(basename "$source_url")"
        local wayland_subprojects="$SRC_DIR/wayland/subprojects"
        local wayland_packagecache="$wayland_subprojects/packagecache"
        mkdir -p "$wayland_subprojects" "$wayland_packagecache"

        # 下载并校验源码包（失败用 fallback）
        if [ -f "$tarball" ]; then
            local actual_hash; actual_hash=$(sha256sum "$tarball" | awk '{print $1}')
            if [ "$actual_hash" = "$source_hash" ]; then
                log "✅ $dep source hash OK, using cached $tarball"
            else
                log "⚠️ $dep source hash mismatch, redownloading"; rm -f "$tarball"
            fi
        fi
        if [ ! -f "$tarball" ]; then
            if ! curl -L "$source_url" -o "$tarball"; then
                if [ -n "$source_fallback_url" ]; then
                    log "⚠️ $dep source_url failed, trying fallback_url: $source_fallback_url"
                    curl -L "$source_fallback_url" -o "$tarball" || { log "❌ download $dep failed (both source_url and fallback_url)"; all_ok=0; continue; }
                else
                    log "❌ download $dep failed (no fallback_url)"; all_ok=0; continue
                fi
            fi
            local actual_hash; actual_hash=$(sha256sum "$tarball" | awk '{print $1}')
            if [ "$actual_hash" != "$source_hash" ]; then
                log "❌ $dep source hash mismatch after download"; rm -f "$tarball"; all_ok=0; continue
            fi
        fi

        # 如有补丁，下载与校验，但不在此解压；交由 Meson 自动应用
        local patch_url patch_hash patch_filename
        patch_url=$(_wrap_field "patch_url" "$wrap_file")
        patch_hash=$(_wrap_field "patch_hash" "$wrap_file")
        patch_filename=$(_wrap_field "patch_filename" "$wrap_file")
        if [ -n "$patch_url" ] && [ -n "$patch_filename" ]; then
            local patch_file="$wrap_dir/$patch_filename"
            if [ -f "$patch_file" ]; then
                local actual_patch_hash; actual_patch_hash=$(sha256sum "$patch_file" | awk '{print $1}')
                if [ "$actual_patch_hash" != "$patch_hash" ]; then
                    log "⚠️ $dep patch hash mismatch, redownloading"; rm -f "$patch_file"
                else
                    log "✅ $dep patch hash OK, using cached $patch_file"
                fi
            fi
            if [ ! -f "$patch_file" ]; then
                curl -L "$patch_url" -o "$patch_file" || { log "❌ download $dep patch failed"; all_ok=0; continue; }
                local actual_patch_hash; actual_patch_hash=$(sha256sum "$patch_file" | awk '{print $1}')
                if [ "$actual_patch_hash" != "$patch_hash" ]; then
                    log "❌ $dep patch hash mismatch after download"; rm -f "$patch_file"; all_ok=0; continue
                fi
            fi
        fi

        # 强制 Meson 使用 wrap 的 directory：删除旧的已解压目录（含无版本名目录），仅提供 wrap + packagecache
        if [ -d "$wayland_subprojects/$dep" ] && [ "$dep" != "$proj_dir" ]; then
            log "ℹ️ Removing stale subproject dir: $wayland_subprojects/$dep"
            rm -rf "$wayland_subprojects/$dep"
        fi
        if [ -d "$wayland_subprojects/$proj_dir" ]; then
            log "ℹ️ Removing existing extracted dir: $wayland_subprojects/$proj_dir"
            rm -rf "$wayland_subprojects/$proj_dir"
        fi

        # 放置 wrap 与缓存
        cp "$wrap_file" "$wayland_subprojects/${dep}.wrap"
        cp "$tarball" "$wayland_packagecache/"
        if [ -n "$patch_filename" ] && [ -f "$wrap_dir/$patch_filename" ]; then
            cp "$wrap_dir/$patch_filename" "$wayland_packagecache/"
        fi

        log "✅ $dep prepared (wrap + packagecache). Meson will extract and apply patch to: $proj_dir"
    done
    popd >/dev/null

    if [ "$all_ok" -ne 1 ]; then
        log "⚠️ Some deps were not fully prepared; Meson may still fallback if possible."
    fi
}

build_compositor() {
    # 调用子脚本构建 compositor 库
    bash "$PROJECT_ROOT/lib/compositor/build.sh" "$OUTPUT_DIR" "$ANDROID_NDK" || {
        log "❌ Failed to build compositor library"
        return 1
    }
}

build_all() {
    # 编译前清理旧的 build.log
    clean_build_logs

    # 先准备交叉依赖
    prepare_cross_deps

    # 使用自定义 pixman 实现（替代 Meson 构建）
    build_pixman_android || {
        log "❌ Failed to build pixman_android"
        return 1
    }

    # 使用更可靠的方式实现wayland编译成功标志，避免重复编译
    # 使用绝对路径确保标志文件位置正确
    local wayland_dir="$(realpath "$SRC_DIR/wayland")"
    local wayland_marker="$wayland_dir/.built"
    
    # 添加调试信息
    log "🔍 Checking wayland build status: marker at $wayland_marker"
    
    if [ -f "$wayland_marker" ]; then
        log "⏭️  wayland already built (found $wayland_marker), skipping compilation"
    else
        log "🔧 Starting wayland compilation..."
        # 直接在wayland目录中创建临时标志文件，确保目录存在
        mkdir -p "$wayland_dir"
        
        # 执行编译
        build_meson wayland \
            --wrap-mode=default \
            -Dscanner=false \
            -Dlibraries=true \
            -Dtests=false \
            -Ddocumentation=false \
            -Dlibffi:exe_static_tramp=true \
            -Dlibffi:c_std=gnu99 \
            -Dlibffi:c_args='-Dasm=__asm__' \
            -Dc_args='-Wno-error=implicit-function-declaration -Wno-error=int-conversion' \
            -Dc_defines='open_memstream(buffer_p, length_p)=NULL'
        
        # 确保创建编译成功标志文件
        if [ $? -eq 0 ]; then
            # 增加调试信息：检查目录权限和路径
            log "🔍 Debug: wayland directory permissions: $(ls -ld "$wayland_dir")"
            log "🔍 Debug: wayland marker path: $wayland_marker"
            log "🔍 Debug: Current user: $(whoami)"
            
            # 显式使用sudo（如果需要）创建标志文件，增加错误捕获
            if touch "$wayland_marker"; then
                log "✅ wayland compilation success flag created: $wayland_marker"
                # 强制刷新文件系统缓存
                sync
                # 验证标志文件是否成功创建
                if [ -f "$wayland_marker" ]; then
                    log "✅ Verified: wayland marker exists and will prevent future recompilation"
                    log "✅ Debug: marker file stats: $(ls -la "$wayland_marker")"
                else
                    # 尝试使用不同方法创建
                    log "❌ Warning: Failed to verify wayland marker creation using standard check"
                    # 尝试使用替代方法确保文件存在
                    echo "built on $(date)" > "$wayland_marker"
                    log "🔄 Retry: Created marker file with content"
                    if [ -s "$wayland_marker" ]; then
                        log "✅ Success: Marker file created with content and will prevent future recompilation"
                    else
                        log "❌ Error: Critical failure - Unable to create marker file even with alternative method"
                    fi
                fi
            else
                log "❌ Error: Failed to create wayland marker file: $wayland_marker"
                # 尝试备用方法
                log "🔄 Attempting alternative method to create marker"
                mkdir -p "$(dirname "$wayland_marker")"
                echo "built" > "$wayland_marker"
                if [ $? -eq 0 ]; then
                    log "✅ Alternative method succeeded: marker file created"
                else
                    log "❌ Critical failure: All methods to create marker failed"
                fi
            fi
        else
            log "❌ wayland compilation failed, no marker created"
        fi
    fi

    build_meson libxkbcommon \
        -Dtests=false

    build_autotools xorgproto
    build_autotools libX11
    build_autotools libXext
    build_autotools libXcursor
    build_autotools libxkbfile

    build_xwayland
    build_wlroots
    
    # 构建 compositor 库
    build_compositor

}
