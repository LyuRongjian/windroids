#!/bin/bash

# WinDroids Build Script - 构建 wlroots + Xwayland + Vulkan 合成器库

set -e

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 确保LIBDIR变量存在
LIBDIR="$PROJECT_ROOT/lib"

# 加载环境变量和工具函数
source "$LIBDIR/env.sh"
source "$LIBDIR/utils.sh"

# 确保日志文件存在
touch "$LOG_FILE"

# 主构建函数
main() {
    log "🚀 开始构建 WinDroids 项目"
    log "📁 项目根目录: $PROJECT_ROOT"
    log "📁 输出目录: $OUTPUT_DIR"
    
    # 加载其他脚本
    source "$LIBDIR/downloads.sh"
    source "$LIBDIR/ndk.sh"
    source "$LIBDIR/build.sh"
    
    # 下载 NDK
    log "⬇️  下载 Android NDK"
    download_ndk
    
    # 创建交叉编译配置文件
    log "⚙️  创建交叉编译配置文件"
    create_cross_and_native
    
    # 下载源码
    log "⬇️  下载组件源码"
    downloads_all
    
    # 构建所有组件
    log "🔧 构建所有组件"
    build_all
    
    log "🎉 WinDroids 构建完成！"
    log "📁 输出目录: $OUTPUT_DIR"
    log "📚 主要产物:"
    log "   - libcompositor.a / libcompositor.so (合成器库)"
    log "   - libwlroots.a (wlroots 静态库)"
    log "   - xwayland (Xwayland 可执行文件)"
    log "📄 头文件: $OUTPUT_DIR/include/compositor.h"
}

# 执行主函数
main | tee -a "$LOG_FILE"