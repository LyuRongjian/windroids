#!/usr/bin/env bash
# Wine integration artifacts

setup_wine_integration() {
    log "🔄 配置 Wine 集成"
    mkdir -p "$OUTPUT_DIR/wine" "$OUTPUT_DIR/wine/lib" "$OUTPUT_DIR/wine/bin" "$OUTPUT_DIR/wine/share"
    cat > "$OUTPUT_DIR/run_wine_app.sh" << 'EOF'
#!/bin/bash
WINDOWSDIR="$(dirname "$0")"
export LD_LIBRARY_PATH="$WINDOWSDIR/lib:$WINDOWSDIR/wine/lib:$LD_LIBRARY_PATH"
export PATH="$WINDOWSDIR/bin:$WINDOWSDIR/wine/bin:$PATH"
XWAYLAND_DISPLAY="wayland-0"
$WINDOWSDIR/bin/xwayland :0 &
XWAYLAND_PID=$!
sleep 2
export DISPLAY=:0
if [ -f "$WINDOWSDIR/wine/bin/wine" ]; then
    $WINDOWSDIR/wine/bin/wine "$@"
else
    echo "请先安装 Wine for Android 到 $WINDOWSDIR/wine"
fi
kill $XWAYLAND_PID
EOF
    chmod +x "$OUTPUT_DIR/run_wine_app.sh"
    cat > "$OUTPUT_DIR/README.md" << EOF
# WinDroids 构建结果
目录: bin/ lib/ include/ wine/ run_wine_app.sh
EOF
    log "✅ Wine integration created"
}
