#!/usr/bin/env bash
# NDK download + cross/native file creation

NDK_SHA1="${NDK_SHA1:-22105e410cf29afcf163760cc95522b9fb981121}"
NDK_TMP_ZIP="$PROJECT_ROOT/android-ndk-${NDK_VERSION}-linux.zip"

download_ndk() {
    log "🔍 NDK SHA1: $NDK_SHA1"
    if [ -f "$NDK_TMP_ZIP" ]; then
        log "✅ NDK zip exists, checking sha1..."
        ACTUAL_SHA1=$(sha1sum "$NDK_TMP_ZIP" | awk '{print $1}')
        if [ "$ACTUAL_SHA1" != "$NDK_SHA1" ]; then
            log "⚠️ SHA1 mismatch, removing $NDK_TMP_ZIP"
            rm -f "$NDK_TMP_ZIP"
        else
            log "✅ SHA1 OK"
        fi
    fi

    if [ ! -f "$NDK_TMP_ZIP" ]; then
        log "⬇️ Downloading NDK -> $NDK_TMP_ZIP"
        curl -# -L "$NDK_URL" -o "$NDK_TMP_ZIP" || { log "❌ NDK download failed"; return 1; }
        ACTUAL_SHA1=$(sha1sum "$NDK_TMP_ZIP" | awk '{print $1}')
        if [ "$ACTUAL_SHA1" != "$NDK_SHA1" ]; then
            log "❌ NDK sha1 mismatch after download"; rm -f "$NDK_TMP_ZIP"; return 1
        fi
    fi

    if [ ! -d "$NDK_DIR/toolchains/llvm" ]; then
        log "📦 Extracting NDK to $NDK_DIR"
        mkdir -p "$NDK_DIR"
        unzip -q "$NDK_TMP_ZIP" -d "$PROJECT_ROOT/ndk-tmp"
        EXTRACTED_DIR=$(find "$PROJECT_ROOT/ndk-tmp" -mindepth 1 -maxdepth 1 -type d | head -n1)
        if [ -z "$EXTRACTED_DIR" ]; then
            log "❌ NDK extract failed"
            return 1
        fi
        cp -a "$EXTRACTED_DIR"/. "$NDK_DIR"/
        rm -rf "$PROJECT_ROOT/ndk-tmp"
        log "✅ NDK installed to $NDK_DIR"
    else
        log "✅ Using existing NDK at $NDK_DIR"
    fi
}

create_cross_and_native() {
    TARGET=aarch64-linux-android
    API=21
    TOOLCHAIN="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64"
    CROSS_FILE="$PROJECT_ROOT/android-cross.txt"
    NATIVE_FILE="$PROJECT_ROOT/native.ini"

    cat > "$CROSS_FILE" << EOF
[binaries]
c = '$TOOLCHAIN/bin/$TARGET$API-clang'
cpp = '$TOOLCHAIN/bin/$TARGET$API-clang++'
ar = '$TOOLCHAIN/bin/llvm-ar'
strip = '$TOOLCHAIN/bin/llvm-strip'
pkg-config = '/usr/bin/pkg-config'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
needs_exe_wrapper = true
has_function_clock_gettime = true
func_suffix = ''
[built-in options]
default_library = 'static'
buildtype = 'release'
b_staticpic = true
[library_fallback]
rt = []
EOF
    log "✅ Cross-file created: $CROSS_FILE"

    cat > "$NATIVE_FILE" << EOF
[binaries]
meson = '$HOME/.local/bin/meson'
ninja = '/usr/bin/ninja'
wayland-scanner = '${WAYLAND_SCANNER:-/usr/bin/wayland-scanner}'
[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
    log "✅ Native-file created: $NATIVE_FILE"
}
