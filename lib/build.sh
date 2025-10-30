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
        meson setup --reconfigure build --cross-file "$PROJECT_ROOT/android-cross.txt" --native-file "$PROJECT_ROOT/native.ini" --prefix "$OUTPUT_DIR" --libdir=lib "$@" || { popd >/dev/null; return 1; }
    else
        meson setup build --cross-file "$PROJECT_ROOT/android-cross.txt" --native-file "$PROJECT_ROOT/native.ini" --prefix "$OUTPUT_DIR" --libdir=lib "$@" || { popd >/dev/null; return 1; }
    fi
    ninja -C build || { popd >/dev/null; return 1; }
    ninja -C build install || { popd >/dev/null; return 1; }
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

build_wlroots() {
    build_meson wlroots -Dexamples=false -Dbackends=drm,headless -Drenderers=gl,gbm -Dgbm=enabled -Dlibffi:exe_static_tramp=true
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

build_all() {
    # 编译前清理旧的 build.log
    clean_build_logs

    # 先准备交叉依赖
    prepare_cross_deps

    # 使用 --wrap-mode=default，允许 Meson 自动 fallback 到 subprojects
    build_meson wayland \
        --wrap-mode=default \
        -Dscanner=false \
        -Dlibraries=true \
        -Ddocumentation=false \
        -Dtests=false \
        -Dlibffi:exe_static_tramp=true \
        -Dlibffi:c_std=gnu99 \
        -Dlibffi:c_args='-Dasm=__asm__'
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
