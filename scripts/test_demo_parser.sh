#!/usr/bin/env bash

# 任意编译或测试错误立即停止。
set -euo pipefail

# 解析 SDK 根目录和临时测试目录。
SDK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEST_ROOT="$SDK_ROOT/demo/build/parser-test"

# 创建独立测试目录，避免覆盖正式 Demo 产物。
rm -rf "$TEST_ROOT"
mkdir -p "$TEST_ROOT"

# 使用主机 C 编译器验证 WiFi 命令解析器。
cc -std=c11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -I"$SDK_ROOT/include" -I"$SDK_ROOT/demo/src" -I"$SDK_ROOT/demo/src/mqtt" -I"$SDK_ROOT/demo/src/mqtt/internal" \
    "$SDK_ROOT/demo/src/mqtt/internal/wifi_command_parser.c" \
    "$SDK_ROOT/demo/tests/test_wifi_command_parser.c" \
    -o "$TEST_ROOT/test_wifi_command_parser"

# 执行 WiFi 命令解析器测试并输出明确结果。
"$TEST_ROOT/test_wifi_command_parser"
printf '%s\n' 'WiFi 命令解析器测试通过。'

# 单独编译 WiFi 文本解码器，避免多个测试文件的 main 函数互相冲突。
cc -std=c11 -Wall -Wextra -Werror \
    -I"$SDK_ROOT/demo/src/network" \
    "$SDK_ROOT/demo/src/network/wifi_text_codec.c" \
    "$SDK_ROOT/demo/tests/test_wifi_text_codec.c" \
    -o "$TEST_ROOT/test_wifi_text_codec"

# 执行 WiFi 中文文本解码测试并输出明确结果。
"$TEST_ROOT/test_wifi_text_codec"
printf '%s\n' 'WiFi 文本解码测试通过。'
