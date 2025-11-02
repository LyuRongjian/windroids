#!/bin/bash
# entrypoint bootstrap for WinDroids build
set -euo pipefail

# load environment (paths, dirs)
. "$(dirname "$0")/lib/env.sh"

# 尽早加载 utils，以便之后可以直接调用 log()
if [ -f "$LIBDIR/utils.sh" ]; then
	# shellcheck source=/dev/null
	. "$LIBDIR/utils.sh"
else
	# 回退实现，保证 log 在 utils 不可用时仍然存在
	log() {
		echo "[$(date +'%H:%M:%S')] $*"
	}
fi

# ensure lib dir exists
if [ ! -d "$PROJECT_ROOT/lib" ] && [ -d "$PROJECT_ROOT/../lib" ]; then
	# nothing
	:
fi

# redirect logs (after PROJECT_ROOT exists)
mkdir -p "$PROJECT_ROOT"
touch "$LOG_FILE"
exec > >(tee -i "$LOG_FILE")
exec 2>&1

log "🚀 WinDroids: Build Started"
log "🔄 Start time: $(date)"

# load helpers and modules
. "$LIBDIR/utils.sh"
. "$LIBDIR/deps.sh"
. "$LIBDIR/downloads.sh"
. "$LIBDIR/ndk.sh"
. "$LIBDIR/build.sh"
. "$LIBDIR/wine.sh"

# Ensure basic dependencies (pipx/meson/git/curl/...); does not install system wayland-scanner
ensure_dependencies

# Download sources and build host tools (wayland-scanner)
downloads_all

# Download and install NDK into project (not /tmp)
download_ndk

# Create cross/native files (uses NDK_DIR)
create_cross_and_native

# Build libraries and components
build_all

# Setup Wine integration artifacts
setup_wine_integration

log "🎉 Build completed!"
log "📁 Output directory: $OUTPUT_DIR"
log "🔄 Finish time: $(date)"