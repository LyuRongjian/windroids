#!/usr/bin/env bash
# ensure system deps and install meson/pipx if missing

ensure_dependencies() {
    # 增加 autoconf 和 automake 以支持 autotools/autogen.sh
    local base_deps=(curl unzip git ninja-build pkg-config python3 python3-pip python3-venv libffi-dev libxml2-dev libexpat1-dev build-essential autoconf automake)
    local missing=()
    for cmd in "${base_deps[@]}"; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            missing+=("$cmd")
        fi
    done
    if [ ${#missing[@]} -gt 0 ]; then
        log "📦 Installing base system deps: ${missing[*]}"
        sudo apt update && sudo apt install -y "${missing[@]}"
    else
        log "✅ Base deps OK"
    fi

    # 不再检查或依赖系统 wayland-scanner：统一从源码构建 host 工具并安装到 HOST_TOOLS_DIR/bin
    log "ℹ️ Skipping system wayland-scanner check; will build host wayland-scanner from source and install to $HOST_TOOLS_DIR/bin"

    # pipx + meson
    if ! command -v pipx >/dev/null 2>&1; then
        python3 -m pip install --user pipx
        python3 -m pipx ensurepath || true
        export PATH="$HOME/.local/bin:$PATH"
    fi
    pipx install meson --force || python3 -m pip install --user meson
    log "✅ meson: $(meson --version 2>/dev/null || echo unknown)"
}
