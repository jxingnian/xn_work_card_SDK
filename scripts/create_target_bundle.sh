#!/usr/bin/env bash

# 任一目标包检查、归档或哈希计算失败时立即停止。
set -euo pipefail

# 解析客户 SDK 根目录。
SDK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# 读取固定 SDK 版本号，禁止目标包名称与交付版本脱节。
SDK_VERSION="$(tr -d '\r\n' <"$SDK_ROOT/VERSION")"

# 保存目标板精简包输出目录。
DIST_ROOT="$SDK_ROOT/dist"

# 固定目标板精简包名称。
ARCHIVE_NAME="workcard-target-$SDK_VERSION.tar.gz"

# 保存正式归档和临时归档路径。
ARCHIVE_FILE="$DIST_ROOT/$ARCHIVE_NAME"
TEMP_FILE="$ARCHIVE_FILE.tmp"

# 校验目标板安装所需目录和脚本完整存在。
for REQUIRED_PATH in \
    "$SDK_ROOT/bin" \
    "$SDK_ROOT/lib" \
    "$SDK_ROOT/runtime" \
    "$SDK_ROOT/demo/bin" \
    "$SDK_ROOT/demo/config" \
    "$SDK_ROOT/scripts/install_target.sh" \
    "$SDK_ROOT/scripts/clean_legacy_target.sh" \
    "$SDK_ROOT/scripts/uninstall_target.sh" \
    "$SDK_ROOT/scripts/install_demo.sh" \
    "$SDK_ROOT/scripts/verify_target_bluetooth.sh" \
    "$SDK_ROOT/VERSION"; do
    if [ ! -e "$REQUIRED_PATH" ]; then
        echo "目标包生成失败: 缺少 $REQUIRED_PATH" >&2
        exit 66
    fi
done

# 创建独立输出目录，不把 x86 工具链、头文件和文档传入小容量目标板。
mkdir -p "$DIST_ROOT"

# 中断时只删除本脚本生成的不完整临时归档。
cleanup_target_bundle()
{
    # 临时归档存在时执行精确删除。
    if [ -f "$TEMP_FILE" ]; then
        rm -f -- "$TEMP_FILE"
    fi
}

# 注册退出清理。
trap cleanup_target_bundle EXIT

# 从 SDK 根目录打包目标板运行必需文件，并统一放入 work_card_SDK 顶层目录。
tar -C "$SDK_ROOT" \
    --transform='s,^,work_card_SDK/,' \
    -cf - \
    bin \
    lib \
    runtime \
    demo/bin \
    demo/config \
    scripts/install_target.sh \
    scripts/clean_legacy_target.sh \
    scripts/uninstall_target.sh \
    scripts/install_demo.sh \
    scripts/verify_target_bluetooth.sh \
    VERSION | gzip -1 >"$TEMP_FILE"

# 原子发布完整目标板归档。
mv "$TEMP_FILE" "$ARCHIVE_FILE"

# 清空临时文件变量，防止退出清理误处理正式归档。
TEMP_FILE=""

# 生成目标板精简包独立 SHA-256。
(
    # 切换到精简包输出目录以使用稳定相对文件名。
    cd "$DIST_ROOT"

    # 写入独立哈希文件供上传前后校验。
    sha256sum "$ARCHIVE_NAME" >"$ARCHIVE_NAME.sha256"
)

# 输出正式文件大小和完整哈希。
ls -lh "$ARCHIVE_FILE"
cat "$ARCHIVE_FILE.sha256"
