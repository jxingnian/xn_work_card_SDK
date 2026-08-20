#!/usr/bin/env bash

# 任一哈希计算失败时立即停止。
set -euo pipefail

# 解析客户 SDK 根目录。
SDK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# 使用临时文件避免中途中断留下半截清单。
MANIFEST_FILE="$SDK_ROOT/MANIFEST.sha256"
TEMP_FILE="$SDK_ROOT/MANIFEST.sha256.tmp"

# 按稳定相对路径顺序计算全部交付文件哈希，排除清单自身和可重建中间目录。
cd "$SDK_ROOT"
find . -type f \
    ! -path './MANIFEST.sha256' \
    ! -path './MANIFEST.sha256.tmp' \
    ! -path './demo/build/*' \
    ! -path './demo/build-cmake/*' \
    ! -path './dist/*' \
    -print0 | sort -z | xargs -0 sha256sum >"$TEMP_FILE"

# 原子发布完整 SHA-256 清单。
mv "$TEMP_FILE" "$MANIFEST_FILE"

# 输出清单条目数量。
wc -l "$MANIFEST_FILE"
