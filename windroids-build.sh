#!/bin/bash
# windroids-build.sh
# WinDroids: Run Windows Apps on Android via Wayland & Wine
# ✅ 真实进度条 | 最新版 Meson | 全自动依赖 | 日志安全 | 国内优化 | 使用系统 wayland-scanner
set -e

# -------------------------------
# 1. 项目配置
# -------------------------------
PROJECT_NAME="windroids"
REPO_DIR=$(pwd)
PROJECT_ROOT="$REPO_DIR/${PROJECT_NAME}"
LOG_FILE="$PROJECT_ROOT/build.log"
NDK_VERSION="r27d"

# ✅ 使用国内镜像链接
NDK_URL="https://developer.android.google.cn/ndk/downloads/android-ndk-${NDK_VERSION}-linux.zip?hl=zh-cn"
NDK_DIR="$PROJECT_ROOT/ndk"
SRC_DIR="$PROJECT_ROOT/src"
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_DIR="$PROJECT_ROOT/output"

# -------------------------------
# 日志函数（必须放在这里！）
# -------------------------------
log() {
    echo "[$(date +'%H:%M:%S')] $1"
}

# -------------------------------
# 2. 安全初始化日志文件
# -------------------------------
if [ -d "$LOG_FILE" ]; then
    echo "🗑️  Removing conflicting directory: $LOG_FILE"
    rm -rf "$LOG_FILE"
fi
mkdir -p "$PROJECT_ROOT"
touch "$LOG_FILE"

echo "🚀 WinDroids: Build Started"
echo "📦 Real progress bars | Latest Meson | Auto-deps"
echo "📄 Log: $LOG_FILE"
echo "🔄 Start time: $(date)"
echo "──────────────────────────────────"

# 重定向所有输出到日志
exec > >(tee -i "$LOG_FILE")
exec 2>&1

# -------------------------------
# 3. 安装基础系统依赖 + pipx + meson + wayland-scanner
# -------------------------------
ensure_dependencies() {
    local base_deps=("curl" "unzip" "git" "ninja-build" "pkg-config" "python3" "python3-pip" "python3-venv" "libffi-dev")
    local missing=()

    for cmd in "${base_deps[@]}"; do
        if ! command -v "$cmd" &> /dev/null; then
            missing+=("$cmd")
        fi
    done

    if [ ${#missing[@]} -gt 0 ]; then
        echo "📦 Installing base system dependencies: ${missing[*]}"
        sudo apt update && sudo apt install -y "${missing[@]}"
    else
        echo "✅ Base system dependencies already installed."
    fi

    # --- 确保 wayland-scanner 可用 ---
    if ! command -v wayland-scanner &> /dev/null; then
        echo "📦 Installing wayland-scanner via apt (libwayland-dev)..."
        sudo apt install -y libwayland-dev
    fi
    local scanner_version=$(wayland-scanner --version 2>/dev/null || echo "unknown")
    echo "✅ wayland-scanner available: $scanner_version"

    # --- 安装 pipx ---
    if ! command -v pipx &> /dev/null; then
        echo "🔧 Installing pipx for Python apps..."
        python3 -m pip install --user pipx
        python3 -m pipx ensurepath
        export PATH="$HOME/.local/bin:$PATH"
    fi

    # --- 安装 Meson ---
    echo "🚀 Installing latest Meson via pipx..."
    pipx install meson --force
    local version=$(meson --version)
    echo "✅ Meson $version installed (via pipx)"

    # --- 安装 expect-dev 用于真实 Git 进度条 ---
    if ! command -v unbuffer &> /dev/null; then
        echo "📦 Installing expect-dev for real Git progress..."
        sudo apt install -y expect-dev
    fi
}

ensure_dependencies

# -------------------------------
# 4. 下载 NDK（带进度条 + 国内镜像 + 完整性校验）
# -------------------------------
# 🔁 请从官网获取真实 SHA1 并替换（示例值无效！）
NDK_SHA1="22105e410cf29afcf163760cc95522b9fb981121"
NDK_TMP_ZIP="/tmp/android-ndk-${NDK_VERSION}-linux.zip"

echo "🔍 NDK 校验码 (SHA1): $NDK_SHA1"
echo "📁 检查本地缓存: $NDK_TMP_ZIP"

# 1. 检查 /tmp 中是否已存在 NDK zip 文件
if [ -f "$NDK_TMP_ZIP" ]; then
    echo "✅ 文件已存在，开始 SHA1 校验..."
    ACTUAL_SHA1=$(sha1sum "$NDK_TMP_ZIP" | awk '{print $1}')
    if [ "$ACTUAL_SHA1" = "$NDK_SHA1" ]; then
        echo "✅ SHA1 校验通过，文件完整，直接使用。"
    else
        echo "❌ SHA1 校验失败！"
        echo "  期望值: $NDK_SHA1"
        echo "  实际值: $ACTUAL_SHA1"
        echo "  文件可能已损坏，正在删除并重新下载..."
        rm -f "$NDK_TMP_ZIP"
    fi
fi

# 2. 如果文件不存在或校验失败，则下载
if [ ! -f "$NDK_TMP_ZIP" ]; then
    echo "⬇️  开始下载 Android NDK $NDK_VERSION..."
    echo "   URL: $NDK_URL"
    if curl -# -L "$NDK_URL" -o "$NDK_TMP_ZIP"; then
        echo "✅ NDK 下载完成。"
    else
        echo "❌ 下载失败！请检查网络连接或尝试手动访问："
        echo "   $NDK_URL"
        exit 1
    fi

    # 再次校验
    echo "🔍 下载完成，再次进行 SHA1 校验..."
    ACTUAL_SHA1=$(sha1sum "$NDK_TMP_ZIP" | awk '{print $1}')
    if [ "$ACTUAL_SHA1" != "$NDK_SHA1" ]; then
        echo "❌ 下载文件 SHA1 校验失败！文件可能不完整或被篡改。"
        rm -f "$NDK_TMP_ZIP"
        exit 1
    fi
    echo "✅ 下载文件 SHA1 校验通过。"
fi

# 3. 解压 NDK（动态识别目录名）
if [ ! -d "$NDK_DIR/toolchains/llvm" ]; then
    echo "📦 正在解压 NDK 到 $NDK_DIR..."
    mkdir -p "$NDK_DIR"
    unzip -q "$NDK_TMP_ZIP" -d "$PROJECT_ROOT/ndk-tmp"
    EXTRACTED_DIR=$(find "$PROJECT_ROOT/ndk-tmp" -mindepth 1 -maxdepth 1 -type d | head -n1)
    if [ -z "$EXTRACTED_DIR" ]; then
        echo "❌ 解压失败：未找到解压后的目录，请检查 ZIP 文件是否为空。"
        exit 1
    fi
    echo "📁 检测到解压目录: $EXTRACTED_DIR"
    mv "$EXTRACTED_DIR"/* "$NDK_DIR" || {
        echo "❌ 移动文件失败，请检查目录权限或磁盘空间。"
        exit 1
    }
    rm -rf "$PROJECT_ROOT/ndk-tmp"
    echo "✅ NDK 已成功安装到 $NDK_DIR"
else
    echo "✅ 使用现有 NDK: $NDK_DIR"
fi

# -------------------------------
# 5. 创建交叉编译文件
# -------------------------------
TARGET=aarch64-linux-android
API=29
TOOLCHAIN="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64"
CROSS_FILE="$PROJECT_ROOT/android-cross.txt"
NATIVE_FILE="$PROJECT_ROOT/native.ini"

cat > "$CROSS_FILE" << EOF
[binaries]
c = '$TOOLCHAIN/bin/$TARGET$API-clang'
cpp = '$TOOLCHAIN/bin/$TARGET$API-clang++'
ar = '$TOOLCHAIN/bin/llvm-ar'
strip = '$TOOLCHAIN/bin/llvm-strip'
pkgconfig = '/usr/bin/pkg-config'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
needs_exe_wrapper = true
EOF

echo "✅ Cross-file created: $CROSS_FILE"

# 创建native.ini文件，用于本地构建工具配置
cat > "$NATIVE_FILE" << EOF
[binaries]
# 本地工具配置，用于运行在主机上的构建工具
meson = '$HOME/.local/bin/meson'
ninja = '/usr/bin/ninja'
wayland-scanner = '/usr/bin/wayland-scanner'
[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

echo "✅ Native-file created: $NATIVE_FILE"

# -------------------------------
# 6. 下载并解压函数
# -------------------------------
mkdir -p "$SRC_DIR"
cd "$SRC_DIR"
download_extract() {
    local name="$1"
    local url="$2"
    local tarball="$SRC_DIR/$(basename "$url")"
    local extract_dir="$SRC_DIR/$name"
    echo "📥 Downloading $name: $url"
    if [ ! -f "$tarball" ]; then
        wget -T 30 -t 3 -O "$tarball" "$url" || {
            echo "❌ Failed to download $url"
            rm -f "$tarball"
            exit 1
        }
    else
        echo "✅ Tarball already exists: $tarball"
    fi
    echo "📦 Extracting to $extract_dir"
    rm -rf "$extract_dir"
    mkdir -p "$extract_dir"
    case "$tarball" in
        *.tar.xz)  tar -xf "$tarball" -C "$extract_dir" --strip-components=1 ;;
        *.tar.gz)  tar -xf "$tarball" -C "$extract_dir" --strip-components=1 ;;
        *.tar.bz2) tar -xf "$tarball" -C "$extract_dir" --strip-components=1 ;;
        *)         echo "❌ Unsupported format: $tarball"; exit 1 ;;
    esac
    echo "✅ $name extracted"
}

# ✅ 修正：使用官方完整 tarball（含 configure）
# ✅ 1. wayland
download_extract wayland "https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.24.0/downloads/wayland-1.24.0.tar.xz"

# ✅ 2. wayland-protocols
download_extract wayland_protocols "https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/1.33/downloads/wayland-protocols-1.33.tar.xz"

# ✅ 3. libdrm
download_extract libdrm "https://dri.freedesktop.org/libdrm/libdrm-2.4.123.tar.xz"

# ✅ 4. libxkbcommon
download_extract libxkbcommon "https://github.com/xkbcommon/libxkbcommon/archive/refs/tags/xkbcommon-1.12.2.tar.gz"

# ✅ 5. pixman（官方发布包，含 configure）
download_extract pixman "https://cairographics.org/releases/pixman-0.42.2.tar.gz"

# ✅ 6. xorg-xproto (via xorgproto)
download_extract xorgproto "https://xorg.freedesktop.org/releases/individual/proto/xorgproto-2024.1.tar.xz"

# ✅ 7. libX11
download_extract libX11 "https://xorg.freedesktop.org/releases/individual/lib/libX11-1.8.6.tar.xz"

# ✅ 8. libXext
download_extract libXext "https://xorg.freedesktop.org/releases/individual/lib/libXext-1.3.5.tar.xz"

# ✅ 9. libXcursor
download_extract libXcursor "https://xorg.freedesktop.org/releases/individual/lib/libXcursor-1.2.2.tar.xz"

# ✅ 10. libxkbfile
download_extract libxkbfile "https://xorg.freedesktop.org/releases/individual/lib/libxkbfile-1.1.2.tar.xz"

# ✅ 11. xwayland (part of xorg-server) - 使用官方 tarball
download_extract xorg_server "https://xorg.freedesktop.org/releases/individual/xserver/xorg-server-21.1.13.tar.xz"

# ✅ 12. wlroots
download_extract wlroots "https://gitlab.freedesktop.org/wlroots/wlroots/-/releases/0.18.3/downloads/wlroots-0.18.3.tar.gz"

echo "✅ 所有源码已下载并解压"

# -------------------------------
# 7. 设置 pkg-config 路径
# -------------------------------
export PKG_CONFIG_LIBDIR="$OUTPUT_DIR/lib/pkgconfig:$SRC_DIR/pkgconfig"
mkdir -p "$PKG_CONFIG_LIBDIR"
export PATH="$TOOLCHAIN/bin:$PATH"

# -------------------------------
# 8. 通用依赖设置函数（用于交叉编译）
# -------------------------------
setup_dependency() {
    local name=$1
    local version=$2
    if [ -z "$name" ] || [ -z "$version" ]; then
        echo "❌ Usage: setup_dependency <name> <version>"
        exit 1
    fi

    echo "📦 Setting up $name $version via WrapDB..."
    local wrap_file="subprojects/${name}.wrap"
    local source_dir="subprojects"
    local tmp_dir="${name}-tmp"
    local patch_tmp_dir="patch-tmp"

    mkdir -p "$source_dir"

    # 1. 安装 .wrap 文件（仅当不存在）
    if [ ! -f "$wrap_file" ]; then
        echo "📥 Installing ${name}.wrap from WrapDB..."
        meson wrap install "$name" > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            echo "❌ Failed to install ${name} wrap. Is 'meson' installed?"
            exit 1
        fi
        echo "✅ ${name}.wrap installed."
    else
        echo "✅ Using existing ${name}.wrap"
    fi

    # 2. 读取 source_url 和 patch_url
    local source_url=$(grep "^source_url" "$wrap_file" | head -n1 | cut -d'=' -f2 | xargs)
    local patch_url=$(grep "^patch_url" "$wrap_file" | head -n1 | cut -d'=' -f2 | xargs)

    if [ -z "$source_url" ]; then
        echo "❌ Failed to read source_url from ${name}.wrap"
        exit 1
    fi

    if [ -n "$patch_url" ]; then
        echo "✅ Patch URL found: $patch_url"
    else
        echo "🟡 ${name}.wrap has no patch_url"
    fi

    local source_filename=$(basename "$source_url")
    local patch_filename=$(basename "$patch_url")
    local source_file="$source_dir/$source_filename"
    local patch_file="$source_dir/$patch_filename"

    # 3. 下载源码（仅当不存在）
    if [ ! -f "$source_file" ]; then
        echo "⬇️  Downloading ${name} source: $source_filename"
        curl -L -o "$source_file" "$source_url" || exit 1
    else
        echo "✅ Using existing $source_filename"
    fi

    # 4. 下载 patch（仅当需要且不存在）
    if [ -n "$patch_url" ] && [ ! -f "$patch_file" ]; then
        echo "⬇️  Downloading ${name} patch: $patch_filename"
        curl -L -o "$patch_file" "$patch_url" || exit 1
    elif [ -n "$patch_url" ]; then
        echo "✅ Using existing $patch_filename"
    fi

    # ✅ 5. 检查 subprojects/$name 是否已存在且非空
    if [ -d "subprojects/$name" ] && [ -n "$(ls -A subprojects/$name 2>/dev/null)" ]; then
        echo "✅ subprojects/$name already exists and populated. Skipping extraction."
    else
        echo "🔄 Extracting $source_file to subprojects/$name..."
        rm -rf "subprojects/$name" "$tmp_dir"
        mkdir -p "$tmp_dir"

        # 解压
        if [[ "$source_file" == *.zip ]]; then
            unzip -q -o "$source_file" -d "$tmp_dir"
        elif [[ "$source_file" == *.tar.gz ]] || [[ "$source_file" == *.tgz ]]; then
            tar -xzf "$source_file" -C "$tmp_dir"
        elif [[ "$source_file" == *.tar.xz ]] || [[ "$source_file" == *.txz ]]; then
            tar -xJf "$source_file" -C "$tmp_dir"
        elif [[ "$source_file" == *.tar.bz2 ]]; then
            tar -xjf "$source_file" -C "$tmp_dir"
        else
            echo "❌ Unsupported archive format: $source_file"
            exit 1
        fi

        local extracted_dir=$(find "$tmp_dir" -mindepth 1 -maxdepth 1 -type d | head -n1)
        if [ -z "$extracted_dir" ]; then
            echo "❌ Failed to extract ${name} source."
            exit 1
        fi

        mv "$extracted_dir" "subprojects/$name"
        rm -rf "$tmp_dir"
    fi

    # ✅ 6. 只有当 patch 存在且 meson.build 不存在时才应用 patch
    if [ -n "$patch_url" ] && [ ! -f "subprojects/$name/meson.build" ]; then
        echo "🔧 Applying patch to $name..."
        rm -rf "$patch_tmp_dir" && mkdir -p "$patch_tmp_dir"
        unzip -q -o "$patch_file" -d "$patch_tmp_dir"

        local meson_build_src=$(find "$patch_tmp_dir" -name "meson.build" | head -n1)
        if [ -z "$meson_build_src" ]; then
            echo "❌ Could not find meson.build in ${name} patch!"
            exit 1
        fi
        cp "$meson_build_src" "subprojects/$name/meson.build"

        local meson_opts=$(find "$patch_tmp_dir" -name "meson_options.txt" | head -n1)
        if [ -n "$meson_opts" ]; then
            cp "$meson_opts" "subprojects/$name/meson_options.txt"
        fi
        rm -rf "$patch_tmp_dir"
    elif [ -n "$patch_url" ]; then
        echo "✅ Patch not needed: subprojects/$name/meson.build already exists."
    else
        if [ ! -f "subprojects/$name/meson.build" ]; then
            echo "❌ subprojects/$name/meson.build not found and no patch provided."
            exit 1
        else
            echo "✅ Found built-in meson.build in source."
        fi
    fi

    echo "✅ $name $version setup complete with full Meson support."
}

# -------------------------------
# 9. 编译函数（移除了 host scanner 构建）
# -------------------------------
build_meson() {
    local dir=$1
    local extra_args=("${@:2}")
    local component_dir="$SRC_DIR/$dir"
    cd "$component_dir"
    echo "🔧 Building $dir"

    # 确保subprojects目录存在
    mkdir -p "$component_dir/subprojects"
    
    # 检查全局subprojects目录并复制依赖文件（如果目标不存在）
    if [ -d "$PROJECT_ROOT/subprojects" ]; then
        echo "📁 使用本地subprojects依赖..."
        
        # 复制所有的.wrap文件（如果目标不存在）
        find "$PROJECT_ROOT/subprojects" -name "*.wrap" -exec cp -n {} "$component_dir/subprojects/" \; 2>/dev/null || true
        
        # 复制所有的源码包文件（如果目标不存在）
        find "$PROJECT_ROOT/subprojects" -name "*.tar.*" -exec cp -n {} "$component_dir/subprojects/" \; 2>/dev/null || true
        
        # 复制已解压的源码目录（如果目标不存在）
        for dep_dir in "$PROJECT_ROOT/subprojects"/*; do
            if [ -d "$dep_dir" ] && [ ! -f "$dep_dir.wrap" ] && [ "$(basename "$dep_dir")" != "packagecache" ]; then
                local target_dir="$component_dir/subprojects/$(basename "$dep_dir")"
                if [ ! -d "$target_dir" ]; then
                    cp -r "$dep_dir" "$target_dir" 2>/dev/null || true
                fi
            fi
        done
    fi

    # 为 wayland 添加子项目依赖
    if [ "$dir" = "wayland" ]; then
        echo "📦 Setting up dependencies with full Meson support..."
        setup_dependency libffi 3.5.2
        setup_dependency expat  2.7.3
        setup_dependency libxml2 2.12.5
    fi

    # ✅ 不再调用 build_host_wayland_scanner
    # 系统 wayland-scanner 会自动被 Meson 调用

    # 不再直接删除build目录，而是使用--reconfigure确保已有依赖不被删除
    if [ -d "build" ]; then
        echo "🔄 重新配置现有构建目录..."
        meson setup --reconfigure build \
            --cross-file "$CROSS_FILE" \
            --native-file "$NATIVE_FILE" \
            --prefix "$OUTPUT_DIR" \
            --libdir lib \
            --wrap-mode=forcefallback \
            -Ddefault_library=static \
            -Db_ndebug=true \
            -Db_staticpic=true \
            -Dc_args="-DFFI_NO_EXEC_TRAMPOLINE=1" \
            -Dcpp_args="-DFFI_NO_EXEC_TRAMPOLINE=1" \
            "${extra_args[@]}"
    else
        echo "🚀 Running meson setup for $dir..."
        meson setup build \
            --cross-file "$CROSS_FILE" \
            --native-file "$NATIVE_FILE" \
            --prefix "$OUTPUT_DIR" \
            --libdir lib \
            --wrap-mode=forcefallback \
            -Ddefault_library=static \
            -Db_ndebug=true \
            -Db_staticpic=true \
            -Dc_args="-DFFI_NO_EXEC_TRAMPOLINE=1" \
            -Dcpp_args="-DFFI_NO_EXEC_TRAMPOLINE=1" \
            "${extra_args[@]}"
    fi

    if [ $? -ne 0 ]; then
        echo "❌ Meson setup failed."
        exit 1
    fi

    echo "🔨 Building $dir..."
    ninja -C build || { echo "❌ Ninja build failed."; exit 1; }
    echo "📦 Installing $dir to $OUTPUT_DIR..."
    ninja -C build install || { echo "❌ Install failed."; exit 1; }
    
    # 保存已构建的依赖回全局subprojects目录（如果有新下载的）
    if [ -d "$component_dir/subprojects" ] && [ -d "$PROJECT_ROOT/subprojects" ]; then
        echo "📊 更新全局subprojects缓存..."
        # 复制新的或更新的.wrap文件
        find "$component_dir/subprojects" -name "*.wrap" -exec cp -n {} "$PROJECT_ROOT/subprojects/" \; 2>/dev/null || true
        
        # 复制新的或更新的源码包
        find "$component_dir/subprojects" -name "*.tar.*" -exec cp -n {} "$PROJECT_ROOT/subprojects/" \; 2>/dev/null || true
        
        # 复制新解压的依赖目录
        for dep_dir in "$component_dir/subprojects"/*; do
            if [ -d "$dep_dir" ] && [ ! -f "$dep_dir.wrap" ] && [ "$(basename "$dep_dir")" != "packagecache" ]; then
                local target_dir="$PROJECT_ROOT/subprojects/$(basename "$dep_dir")"
                if [ ! -d "$target_dir" ]; then
                    cp -r "$dep_dir" "$target_dir" 2>/dev/null || true
                fi
            fi
        done
    fi
    
    echo "✅ $dir built and installed successfully!"
}

# ✅ 新增：build_autotools 函数（用于 pixman, xorgproto, libX*, xorg-server）
build_autotools() {
    local dir=$1
    shift
    cd "$SRC_DIR/$dir"
    if [ ! -f "configure" ] && [ -f "autogen.sh" ]; then
        echo "🔧 Running autogen.sh for $dir"
        ./autogen.sh --no-configure "$@"
    fi
    ./configure --host=aarch64-linux-android \
                --prefix="$OUTPUT_DIR" \
                --disable-shared \
                --enable-static \
                "$@"
    make -j$(nproc)
    make install
}

# -------------------------------
# 10. 编译依赖库（按照正确的依赖顺序）
# -------------------------------
# 辅助函数：检查构建结果
check_build_result() {
    local component=$1
    local expected_path=$2
    if [ -f "$expected_path" ]; then
        echo "✅ $component 构建成功：$expected_path"
        return 0
    else
        echo "❌ $component 构建失败：未找到 $expected_path"
        return 1
    fi
}

# 构建缓存检查函数
build_if_not_exists() {
    local builder=$1
    local component=$2
    local expected_path=$3
    shift 3
    
    if [ -f "$expected_path" ]; then
        echo "✅ 跳过 $component 构建，已存在：$expected_path"
        return 0
    else
        echo "🔨 开始构建 $component"
        $builder $component "$@"
        return $?
    fi
}

# 按照依赖顺序构建（Meson项目）
build_if_not_exists build_meson wayland "$OUTPUT_DIR/lib/libwayland-client.so" \
    --wrap-mode=nodownload \
    --wrap-mode=nofallback \
    -Dscanner=false \
    -Dlibraries=true \
    -Ddocumentation=false \
    -Ddtd_validation=false \
    -Dlibffi:exe_static_tramp=true

build_if_not_exists build_meson libdrm "$OUTPUT_DIR/lib/libdrm.so"
build_if_not_exists build_meson pixman "$OUTPUT_DIR/lib/libpixman-1.a"
build_if_not_exists build_meson libxkbcommon "$OUTPUT_DIR/lib/libxkbcommon.a"

# Autotools项目（按照依赖顺序）
build_if_not_exists build_autotools xorgproto "$OUTPUT_DIR/include/X11/xproto.h"
build_if_not_exists build_autotools libX11 "$OUTPUT_DIR/lib/libX11.a"
build_if_not_exists build_autotools libXext "$OUTPUT_DIR/lib/libXext.a"
build_if_not_exists build_autotools libXcursor "$OUTPUT_DIR/lib/libXcursor.a"
build_if_not_exists build_autotools libxkbfile "$OUTPUT_DIR/lib/libxkbfile.a"

# -------------------------------
# 12. 编译 XWayland（xorg-server）
# -------------------------------
build_xwayland() {
    local component="XWayland"
    local expected_path="$OUTPUT_DIR/bin/xwayland"
    
    if [ -f "$expected_path" ]; then
        echo "✅ 跳过 $component 构建，已存在：$expected_path"
        return 0
    fi
    
    echo "🔧 Building $component"
    cd "$SRC_DIR/xorg_server" || { echo "❌ 无法进入 xorg_server 目录"; return 1; }
    
    # 确保干净的构建环境
    make clean || true
    rm -f "configure"
    
    # 生成配置脚本
    if [ ! -f "configure" ] && [ -f "autogen.sh" ]; then
        echo "🔧 运行 autogen.sh"
        ./autogen.sh --host=aarch64-linux-android \
            --enable-xwayland \
            --disable-xorg \
            --disable-dri \
            --disable-glamor \
            --without-dtrace \
            --prefix="$OUTPUT_DIR" \
            CFLAGS="-I$OUTPUT_DIR/include" \
            LDFLAGS="-L$OUTPUT_DIR/lib" || { echo "❌ autogen.sh 失败"; return 1; }
    fi
    
    # 配置
    echo "🔧 配置 $component"
    ./configure --host=aarch64-linux-android \
                --prefix="$OUTPUT_DIR" \
                --enable-xwayland \
                --disable-xorg \
                --disable-dri \
                --disable-glamor \
                --without-dtrace \
                CFLAGS="-I$OUTPUT_DIR/include" \
                LDFLAGS="-L$OUTPUT_DIR/lib" || { echo "❌ configure 失败"; return 1; }
    
    # 构建
    echo "🔨 编译 $component"
    make -j$(nproc) || { echo "❌ make 失败"; return 1; }
    
    # 安装
    echo "📦 安装 $component"
    make install || { echo "❌ make install 失败"; return 1; }
    
    # 确保可执行文件存在
    mkdir -p "$OUTPUT_DIR/bin"
    cp hw/xwayland/xwayland "$OUTPUT_DIR/bin/xwayland" || { echo "❌ 复制 xwayland 失败"; return 1; }
    
    check_build_result "$component" "$expected_path"
    return $?
}

build_xwayland

# -------------------------------
# 13. 编译 wlroots
# -------------------------------
build_wlroots() {
    local component="wlroots"
    local expected_path="$OUTPUT_DIR/lib/libwlroots.so"
    
    if [ -f "$expected_path" ]; then
        echo "✅ 跳过 $component 构建，已存在：$expected_path"
        return 0
    fi
    
    echo "🔧 Building $component"
    cd "$SRC_DIR/wlroots" || { echo "❌ 无法进入 wlroots 目录"; return 1; }
    
    # 确保干净的构建环境
    rm -rf build
    
    # 设置环境变量确保找到正确的依赖
    export PKG_CONFIG_PATH="$OUTPUT_DIR/lib/pkgconfig:$PKG_CONFIG_PATH"
    export CFLAGS="-I$OUTPUT_DIR/include $CFLAGS"
    export LDFLAGS="-L$OUTPUT_DIR/lib $LDFLAGS"
    
    # 配置 Meson
    echo "🔧 配置 $component"
    meson setup build \
        --cross-file "$CROSS_FILE" \
        --native-file "$NATIVE_FILE" \
        --prefix "$OUTPUT_DIR" \
        --libdir lib \
        --wrap-mode=nodownload \
        -Dxwayland=true \
        -Dexamples=false \
        -Dbackends=drm,headless \
        -Drenderers=gl,gbm \
        -Dgbm=enabled \
        -Dlibffi:exe_static_tramp=true || { echo "❌ meson setup 失败"; return 1; }
    
    # 构建
    echo "🔨 编译 $component"
    ninja -C build || { echo "❌ ninja 构建失败"; return 1; }
    
    # 安装
    echo "📦 安装 $component"
    ninja -C build install || { echo "❌ ninja install 失败"; return 1; }
    
    # 确保库文件存在
    mkdir -p "$OUTPUT_DIR/lib"
    cp build/libwlroots.so "$OUTPUT_DIR/lib/libwlroots.so" || { echo "❌ 复制 libwlroots.so 失败"; return 1; }
    
    check_build_result "$component" "$expected_path"
    return $?
}

build_wlroots

# -------------------------------
# 14. Wine 集成配置
# -------------------------------
setup_wine_integration() {
    echo "🔄 配置 Wine 集成"
    
    # 创建 Wine 集成目录
    mkdir -p "$OUTPUT_DIR/wine"
    mkdir -p "$OUTPUT_DIR/wine/lib"
    mkdir -p "$OUTPUT_DIR/wine/bin"
    mkdir -p "$OUTPUT_DIR/wine/share"
    
    # 创建 Wine 集成脚本
    cat > "$OUTPUT_DIR/run_wine_app.sh" << 'EOF'
#!/bin/bash
# Wine 应用运行脚本

# 设置环境变量
WINDOWSDIR="$(dirname "$0")"
export LD_LIBRARY_PATH="$WINDOWSDIR/lib:$WINDOWSDIR/wine/lib:$LD_LIBRARY_PATH"
export PATH="$WINDOWSDIR/bin:$WINDOWSDIR/wine/bin:$PATH"

# 启动 XWayland
XWAYLAND_DISPLAY="wayland-0"
$WINDOWSDIR/bin/xwayland :0 &
XWAYLAND_PID=$!
sleep 2  # 等待 XWayland 启动

# 设置显示变量
export DISPLAY=:0

# 启动 Wine 应用（用户需要自己安装 Wine）
if [ -f "$WINDOWSDIR/wine/bin/wine" ]; then
    echo "启动 Wine 应用: $@"
    $WINDOWSDIR/wine/bin/wine "$@"
else
    echo "错误: 请先在 $WINDOWSDIR/wine 目录下安装 Wine for Android"
fi

# 清理
kill $XWAYLAND_PID
EOF
    
    chmod +x "$OUTPUT_DIR/run_wine_app.sh"
    echo "✅ Wine 集成脚本创建成功: $OUTPUT_DIR/run_wine_app.sh"
    
    # 创建安装指南
    cat > "$OUTPUT_DIR/README.md" << 'EOF'
# WinDroids 构建结果

## 目录结构
- `bin/`: 可执行文件（如 xwayland）
- `lib/`: 库文件（如 libwlroots.so）
- `include/`: 头文件
- `wine/`: Wine 集成目录
- `run_wine_app.sh`: 运行 Wine 应用的启动脚本

## 使用说明
1. 将构建结果复制到 Android 设备
2. 在 `wine/` 目录下安装 Wine for Android
3. 运行 `./run_wine_app.sh 你的Windows应用.exe`

## 注意事项
- 确保设备支持 Wayland
- Wine 需要单独下载和安装
- 部分 Windows 应用可能需要额外配置
EOF
    
    echo "✅ 安装指南创建成功: $OUTPUT_DIR/README.md"
}

# 执行 Wine 集成配置
setup_wine_integration

# -------------------------------
# 15. 构建完成
# -------------------------------
echo "🎉 Build completed!"
echo "📁 Output directory: $OUTPUT_DIR"
echo "🔄 Finish time: $(date)"

# 显示构建摘要
echo "\n📊 构建摘要:"
echo "=================================="
echo "✅ 主要组件:"
[ -f "$OUTPUT_DIR/bin/xwayland" ] && echo "  * XWayland: $OUTPUT_DIR/bin/xwayland"
[ -f "$OUTPUT_DIR/lib/libwlroots.so" ] && echo "  * wlroots: $OUTPUT_DIR/lib/libwlroots.so"
[ -f "$OUTPUT_DIR/lib/libwayland-client.so" ] && echo "  * Wayland: $OUTPUT_DIR/lib/libwayland-client.so"
[ -f "$OUTPUT_DIR/run_wine_app.sh" ] && echo "  * Wine 启动脚本: $OUTPUT_DIR/run_wine_app.sh"
echo "=================================="

# 显示后续步骤
echo "\n📝 后续步骤:"
echo "1. 将构建结果复制到 Android 设备"
echo "2. 安装 Wine for Android"
echo "3. 参考 $OUTPUT_DIR/README.md 获取详细使用说明"