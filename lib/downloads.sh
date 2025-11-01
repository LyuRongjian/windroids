#!/usr/bin/env bash
# package cache helpers and source downloads

optimize_package_cache() {
    mkdir -p "$PACKAGE_CACHE"
    shopt -s nullglob
    local files=( "$COMMON_SUBPROJECTS"/*.{zip,tgz,tar.gz,tar.bz2,tar.xz} )
    shopt -u nullglob
    for f in "${files[@]}"; do
        [ -f "$f" ] || continue
        mv "$f" "$PACKAGE_CACHE/"
        log "✅ moved $(basename "$f") to packagecache"
    done
}

download_extract() {
    local name="$1" url="$2"
    local tarball="$SRC_DIR/$(basename "$url")"
    local outdir="$SRC_DIR/$name"
    log "📥 $name <- $url"
    if [ ! -f "$tarball" ]; then
        wget -T30 -t3 -O "$tarball" "$url" || { log "❌ download failed: $url"; return 1; }
    fi
    rm -rf "$outdir"
    mkdir -p "$outdir"
    case "$tarball" in
        *.tar.xz) tar -xf "$tarball" -C "$outdir" --strip-components=1 ;;
        *.tar.gz) tar -xf "$tarball" -C "$outdir" --strip-components=1 ;;
        *.tar.bz2) tar -xf "$tarball" -C "$outdir" --strip-components=1 ;;
        *) log "❌ Unsupported: $tarball"; return 1 ;;
    esac
    log "✅ extracted $name"
}

build_host_wayland_scanner() {
    log "🔧 Build host wayland-scanner"
    if [ ! -d "$SRC_DIR/wayland" ]; then
        log "❌ wayland source missing"
        return 1
    fi
    pushd "$SRC_DIR/wayland" >/dev/null
    rm -rf build-host || true
    meson setup build-host --prefix="$HOST_TOOLS_DIR" --libdir=lib -Ddocumentation=false || { popd >/dev/null; return 1; }
    ninja -C build-host || { popd >/dev/null; return 1; }
    ninja -C build-host install || { popd >/dev/null; return 1; }
    popd >/dev/null

    local scanner="$HOST_TOOLS_DIR/bin/wayland-scanner"
    if [ -x "$scanner" ]; then
        export PATH="$HOST_TOOLS_DIR/bin:$PATH"
        export WAYLAND_SCANNER="$scanner"
        log "✅ host wayland-scanner at $scanner"

        # 生成与实际工具版本匹配的 pkg-config 文件，优先被 meson/pkg-config 使用
        local pkgdir="$HOST_TOOLS_DIR/lib/pkgconfig"
        mkdir -p "$pkgdir"
        local scanner_version
        scanner_version=$("$scanner" --version 2>&1 | grep -oE '([0-9]+\.){1,2}[0-9]+' | head -n1 || echo "unknown")
        cat > "$pkgdir/wayland-scanner.pc" << EOF
prefix=$HOST_TOOLS_DIR
exec_prefix=\${prefix}
bindir=\${exec_prefix}/bin
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: wayland-scanner
Description: Wayland scanner tool (built for host)
Version: $scanner_version
Requires:
Libs:
Cflags:

# 路径变量，meson 的检测脚本可读取该变量来定位可执行文件
wayland_scanner=$scanner
EOF
        log "✅ created pkg-config file for wayland-scanner (version: $scanner_version) at $pkgdir/wayland-scanner.pc"

        return 0
    else
        log "❌ wayland-scanner not found after build"
        return 1
    fi
}

downloads_all() {
    optimize_package_cache
    # downloads (same list as before)
    download_extract wayland "https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.24.0/downloads/wayland-1.24.0.tar.xz"
    download_extract wayland_protocols "https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/1.33/downloads/wayland-protocols-1.33.tar.xz"
    download_extract libxkbcommon "https://github.com/xkbcommon/libxkbcommon/archive/refs/tags/xkbcommon-1.12.2.tar.gz"
    download_extract xorgproto "https://xorg.freedesktop.org/releases/individual/proto/xorgproto-2024.1.tar.xz"
    download_extract libX11 "https://xorg.freedesktop.org/releases/individual/lib/libX11-1.8.6.tar.xz"
    download_extract libXext "https://xorg.freedesktop.org/releases/individual/lib/libXext-1.3.5.tar.xz"
    download_extract libXcursor "https://xorg.freedesktop.org/releases/individual/lib/libXcursor-1.2.2.tar.xz"
    download_extract libxkbfile "https://xorg.freedesktop.org/releases/individual/lib/libxkbfile-1.1.2.tar.xz"
    download_extract xorg_server "https://xorg.freedesktop.org/releases/individual/xserver/xorg-server-21.1.13.tar.xz"
    download_extract wlroots "https://gitlab.freedesktop.org/wlroots/wlroots/-/releases/0.18.3/downloads/wlroots-0.18.3.tar.gz"

    # build host tool
    build_host_wayland_scanner
}
