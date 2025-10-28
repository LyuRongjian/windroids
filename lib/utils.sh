#!/usr/bin/env bash
# Utility functions

log() {
    echo "[$(date +'%H:%M:%S')] $*"
}

get_dependency_version() {
    local dep_name=$1
    log "📦 获取 wrap 版本信息: $dep_name"
    local full_output
    full_output=$(meson wrap info "$dep_name" 2>&1) || full_output="$full_output"
    printf "%s\n" "$full_output" | head -n 20 >/dev/stderr

    local latest
    latest=$(printf "%s" "$full_output" | grep -oE '[0-9]+(\.[0-9]+){1,2}' | head -n1 || true)
    if [ -n "$latest" ]; then
        printf "%s\n" "$latest"
        return 0
    fi

    case "$dep_name" in
        libffi) printf "3.4.4\n" ;;
        expat) printf "2.5.0\n" ;;
        libxml2) printf "2.11.5\n" ;;
        *) printf "1.0.0\n" ;;
    esac
}

verify_hash() {
    local file="$1"; local expected="$2"
    if [ -z "$expected" ]; then
        log "🟡 无哈希，跳过校验: $file"
        return 0
    fi
    if command -v sha256sum >/dev/null 2>&1; then
        local actual
        actual=$(sha256sum "$file" | awk '{print $1}')
        if [ "$actual" = "$expected" ]; then
            log "✅ 哈希校验通过: $file"
            return 0
        else
            log "❌ 哈希不匹配: $file (expected $expected, got $actual)"
            return 1
        fi
    else
        log "⚠️ sha256sum 不可用，跳过校验"
        return 0
    fi
}
