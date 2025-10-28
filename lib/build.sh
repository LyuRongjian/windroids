#!/usr/bin/env bash
# Build helpers and main build_all

build_meson() {
    local dir=$1
    shift
    log "🔧 meson build: $dir"
    pushd "$SRC_DIR/$dir" >/dev/null

    # 自动构建 subprojects/libffi，如果没有 libffi.pc
    if [ "$dir" = "wayland" ]; then
        if [ ! -f "subprojects/libffi/build/libffi.pc" ] && [ -d "subprojects/libffi" ]; then
            log "🔧 auto-building libffi for cross (subproject)"
            pushd "subprojects/libffi" >/dev/null
            meson setup build --cross-file "$PROJECT_ROOT/android-cross.txt" --prefix "$OUTPUT_DIR" --libdir=lib || true
            ninja -C build || true
            ninja -C build install || true
            popd >/dev/null
        fi
    fi

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
        meson setup --reconfigure build --cross-file "$PROJECT_ROOT/android-cross.txt" --native-file "$PROJECT_ROOT/native.ini" --prefix "$OUTPUT_DIR" --libdir=lib "$@" || { popd >/dev/null; return 1; }
    else
        meson setup build --cross-file "$PROJECT_ROOT/android-cross.txt" --native-file "$PROJECT_ROOT/native.ini" --prefix "$OUTPUT_DIR" --libdir=lib "$@" || { popd >/dev/null; return 1; }
    fi
    ninja -C build || { popd >/dev/null; return 1; }
    ninja -C build install || { popd >/dev/null; return 1; }
    popd >/dev/null
}

build_autotools() {
    local dir=$1
    shift
    log "🔧 autotools build: $dir"
    pushd "$SRC_DIR/$dir" >/dev/null
    if [ -f autogen.sh ] && [ ! -f configure ]; then
        ./autogen.sh --no-configure || true
    fi
    ./configure --host=aarch64-linux-android --prefix="$OUTPUT_DIR" --disable-shared --enable-static "$@" || { popd >/dev/null; return 1; }
    make -j"$(nproc)" || { popd >/dev/null; return 1; }
    make install || { popd >/dev/null; return 1; }
    popd >/dev/null
}

build_xwayland() {
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
    popd >/dev/null
}

build_wlroots() {
    build_meson wlroots -Dexamples=false -Dbackends=drm,headless -Drenderers=gl,gbm -Dgbm=enabled -Dlibffi:exe_static_tramp=true
}

prepare_cross_deps() {
    local wrap_dir="$SRC_DIR/meson-wrap"
    mkdir -p "$wrap_dir"
    pushd "$wrap_dir" >/dev/null

    mkdir -p subprojects

    local all_ok=1
    for dep in libffi expat libxml2; do
        meson wrap install "$dep" || true

        local wrap_file="subprojects/${dep}.wrap"
        if [ ! -f "$wrap_file" ]; then
            log "❌ wrap file for $dep not found in $wrap_dir/subprojects/"
            all_ok=0
            continue
        fi

        # 解析 wrap 文件中的 source_url/source_hash
        local source_url
        source_url=$(grep "^source_url" "$wrap_file" | head -n1 | cut -d'=' -f2 | xargs)
        local source_hash
        source_hash=$(grep "^source_hash" "$wrap_file" | head -n1 | cut -d'=' -f2 | xargs)
        local tarball
        tarball="$wrap_dir/$(basename "$source_url")"

        # 下载并校验源码包
        if [ -f "$tarball" ]; then
            local actual_hash
            actual_hash=$(sha256sum "$tarball" | awk '{print $1}')
            if [ "$actual_hash" = "$source_hash" ]; then
                log "✅ $dep source hash OK, using cached $tarball"
            else
                log "⚠️ $dep source hash mismatch, redownloading"
                rm -f "$tarball"
            fi
        fi
        if [ ! -f "$tarball" ]; then
            # 优先下载 source_url，失败则尝试 source_fallback_url
            if ! curl -L "$source_url" -o "$tarball"; then
                local fallback_url
                fallback_url=$(grep "^source_fallback_url" "$wrap_file" | head -n1 | cut -d'=' -f2 | xargs)
                if [ -n "$fallback_url" ]; then
                    log "⚠️ $dep source_url failed, trying fallback_url: $fallback_url"
                    if ! curl -L "$fallback_url" -o "$tarball"; then
                        log "❌ download $dep failed (both source_url and fallback_url)"
                        continue
                    fi
                else
                    log "❌ download $dep failed (no fallback_url)"
                    continue
                fi
            fi
            local actual_hash
            actual_hash=$(sha256sum "$tarball" | awk '{print $1}')
            if [ "$actual_hash" != "$source_hash" ]; then
                log "❌ $dep source hash mismatch after download"
                rm -f "$tarball"
                continue
            fi
        fi

        # 解压源码
        local src_dir="$wrap_dir/$dep-src"
        rm -rf "$src_dir"
        mkdir -p "$src_dir"
        case "$tarball" in
            *.tar.xz)  tar -xf "$tarball" -C "$src_dir" --strip-components=1 ;;
            *.tar.gz)  tar -xf "$tarball" -C "$src_dir" --strip-components=1 ;;
            *.tar.bz2) tar -xf "$tarball" -C "$src_dir" --strip-components=1 ;;
            *.zip)     unzip -q "$tarball" -d "$src_dir" ;;
            *)         log "❌ Unsupported archive format: $tarball"; continue ;;
        esac

        # 打补丁（如果 wrap 文件有 patch_url/patch_hash）
        local patch_url
        patch_url=$(grep "^patch_url" "$wrap_file" | head -n1 | cut -d'=' -f2 | xargs)
        local patch_hash
        patch_hash=$(grep "^patch_hash" "$wrap_file" | head -n1 | cut -d'=' -f2 | xargs)
        local patch_filename
        patch_filename=$(grep "^patch_filename" "$wrap_file" | head -n1 | cut -d'=' -f2 | xargs)
        if [ -n "$patch_url" ] && [ -n "$patch_filename" ]; then
            # 使用 patch_filename 作为本地补丁文件名
            local patch_file="$wrap_dir/$patch_filename"
            if [ -f "$patch_file" ]; then
                local actual_patch_hash
                actual_patch_hash=$(sha256sum "$patch_file" | awk '{print $1}')
                if [ "$actual_patch_hash" = "$patch_hash" ]; then
                    log "✅ $dep patch hash OK, using cached $patch_file"
                else
                    log "⚠️ $dep patch hash mismatch, redownloading"
                    rm -f "$patch_file"
                fi
            fi
            if [ ! -f "$patch_file" ]; then
                curl -L "$patch_url" -o "$patch_file" || { log "❌ download $dep patch failed"; continue; }
                local actual_patch_hash
                actual_patch_hash=$(sha256sum "$patch_file" | awk '{print $1}')
                if [ "$actual_patch_hash" != "$patch_hash" ]; then
                    log "❌ $dep patch hash mismatch after download"
                    rm -f "$patch_file"
                    continue
                fi
            fi
            # 解压补丁到临时目录
            local patch_tmp_dir="$wrap_dir/${dep}-patch-tmp"
            rm -rf "$patch_tmp_dir"
            mkdir -p "$patch_tmp_dir"
            unzip -q "$patch_file" -d "$patch_tmp_dir"
            # 自动复制 meson.build 和 meson_options.txt 到源码根目录
            local meson_build_src
            meson_build_src=$(find "$patch_tmp_dir" -name "meson.build" | head -n1)
            if [ -n "$meson_build_src" ]; then
                cp "$meson_build_src" "$src_dir/meson.build"
                log "✅ $dep meson.build patched"
            fi
            local meson_opts_src
            meson_opts_src=$(find "$patch_tmp_dir" -name "meson_options.txt" | head -n1)
            if [ -n "$meson_opts_src" ]; then
                cp "$meson_opts_src" "$src_dir/meson_options.txt"
                log "✅ $dep meson_options.txt patched"
            fi
            rm -rf "$patch_tmp_dir"
            log "✅ $dep patch applied"
        else
            # 没有补丁时也要保证后续流程继续
            log "ℹ️ $dep has no patch, skipping patch step"
        fi

        # 打补丁后，修复 asm 语法兼容性（仅针对 aarch64/ffi.c）
        if [ "$dep" = "libffi" ]; then
            local ffi_c="$src_dir/src/aarch64/ffi.c"
            if [ -f "$ffi_c" ]; then
                # 只替换 asm 关键字为 __asm__，避免误替换字符串和注释
                sed -i 's/\<asm\>/__asm__/g' "$ffi_c"
                log "✅ libffi aarch64/ffi.c asm syntax patched for clang"
            fi
        fi

        # 只需同步到 wayland/subprojects，剩下的交叉编译交给 Meson
        local wayland_subprojects="$SRC_DIR/wayland/subprojects"
        local wayland_packagecache="$wayland_subprojects/packagecache"
        mkdir -p "$wayland_subprojects"
        mkdir -p "$wayland_packagecache"
        rm -rf "$wayland_subprojects/$dep"
        cp -r "$src_dir" "$wayland_subprojects/$dep"
        cp "$wrap_file" "$wayland_subprojects/${dep}.wrap"
        # 修正：复制源码包和补丁到 packagecache，必须确保文件存在再复制
        if [ -f "$tarball" ]; then
            cp "$tarball" "$wayland_packagecache/"
            log "✅ $dep source copied to wayland/subprojects/packagecache"
        else
            log "⚠️ $dep source tarball missing, not copied to packagecache"
        fi
        if [ -n "$patch_filename" ] && [ -f "$wrap_dir/$patch_filename" ]; then
            cp "$wrap_dir/$patch_filename" "$wayland_packagecache/"
            log "✅ $dep patch copied to wayland/subprojects/packagecache"
        fi
        log "✅ $dep prepared for wayland subproject and packagecache"
    done
    popd >/dev/null
    # 修正：不要用 return/exit，直接让函数自然结束（shell函数默认返回0），主流程会继续执行
    # 检查 all_ok，如果有依赖失败则在 build_all 里判断
}

build_all() {
    # 先准备交叉依赖
    prepare_cross_deps
    # 检查 prepare_cross_deps 是否全部成功
    if [ $? -ne 0 ]; then
        log "❌ prepare_cross_deps failed, aborting build_all."
        exit 1
    fi

    # 使用 --wrap-mode=default，允许 Meson 自动 fallback 到 subprojects（只要本地包和补丁已准备好不会重复下载）
    build_meson wayland \
        --wrap-mode=default \
        -Dscanner=false \
        -Dlibraries=true \
        -Ddocumentation=false \
        -Dtests=false \
        -Dlibffi:exe_static_tramp=true
    build_meson libdrm
    build_meson pixman
    build_meson libxkbcommon

    build_autotools xorgproto
    build_autotools libX11
    build_autotools libXext
    build_autotools libXcursor
    build_autotools libxkbfile

    build_xwayland

    build_wlroots
}
