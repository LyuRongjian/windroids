#!/usr/bin/env bash
# Environment and common paths for WinDroids build
PROJECT_NAME="windroids"
REPO_DIR="$(pwd)"
PROJECT_ROOT="$REPO_DIR/${PROJECT_NAME}"
LOG_FILE="$PROJECT_ROOT/build.log"
NDK_VERSION="${NDK_VERSION:-r27d}"
NDK_URL="https://developer.android.google.cn/ndk/downloads/android-ndk-${NDK_VERSION}-linux.zip?hl=zh-cn"

NDK_DIR="$PROJECT_ROOT/ndk"
SRC_DIR="$PROJECT_ROOT/src"
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_DIR="$PROJECT_ROOT/output"
COMMON_SUBPROJECTS="$PROJECT_ROOT/subprojects"
SUBPROJECTS_DIR="$COMMON_SUBPROJECTS"
PACKAGE_CACHE="$COMMON_SUBPROJECTS/packagecache"
HOST_TOOLS_DIR="$PROJECT_ROOT/host-tools"

# lib dir for split scripts
LIBDIR="$(dirname "${BASH_SOURCE[0]}")"
mkdir -p "$COMMON_SUBPROJECTS" "$PACKAGE_CACHE" "$HOST_TOOLS_DIR/bin" "$OUTPUT_DIR" "$SRC_DIR"
export PATH="$HOST_TOOLS_DIR/bin:$PATH"
