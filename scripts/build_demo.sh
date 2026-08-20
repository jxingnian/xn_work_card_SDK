#!/usr/bin/env bash

# 任意编译或链接错误立即停止。
set -euo pipefail

# 解析客户 SDK 根目录。
SDK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# 加载固定工具链版本和自动定位函数。
# shellcheck source=toolchain_env.sh
source "$SDK_ROOT/scripts/toolchain_env.sh"

# 兼容 TOOLCHAIN_ROOT 和 CMake 使用的 WORKCARD_TOOLCHAIN_ROOT 显式配置。
REQUESTED_TOOLCHAIN_ROOT="${TOOLCHAIN_ROOT:-${WORKCARD_TOOLCHAIN_ROOT:-}}"

# 优先使用显式目录，否则自动使用 setup_dev_env.sh 的默认安装位置。
workcard_resolve_toolchain "$REQUESTED_TOOLCHAIN_ROOT"

# 保存已经验证可用的固定 ARM musl 工具链目录。
TOOLCHAIN_ROOT="$WORKCARD_RESOLVED_TOOLCHAIN_ROOT"

# 保存交叉编译器前缀。
TOOLCHAIN_PREFIX="$TOOLCHAIN_ROOT/bin/arm-linux-musleabi-"

# 保存 Demo 源码和独立构建目录。
DEMO_ROOT="$SDK_ROOT/demo"
BUILD_ROOT="$DEMO_ROOT/build"

# 清理并重新创建独立构建目录。
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT/obj" "$BUILD_ROOT/bin"

# 保存 Demo 的模块化 C 源文件。
DEMO_SOURCES=(
    "$DEMO_ROOT/src/main.c"
    "$DEMO_ROOT/src/bluetooth/bluetooth_demo.c"
    "$DEMO_ROOT/src/cloud/cloud_demo.c"
    "$DEMO_ROOT/src/cloud/internal/config.c"
    "$DEMO_ROOT/src/cloud/internal/media_packet.c"
    "$DEMO_ROOT/src/cloud/internal/time_utils.c"
    "$DEMO_ROOT/src/cloud/internal/websocket_client.c"
    "$DEMO_ROOT/src/mqtt/mqtt_demo.c"
    "$DEMO_ROOT/src/mqtt/internal/desired_config_parser.c"
    "$DEMO_ROOT/src/mqtt/internal/mqtt_tls_platform.c"
    "$DEMO_ROOT/src/mqtt/internal/wifi_command_parser.c"
    "$DEMO_ROOT/src/network/wifi_profile_store.c"
    "$DEMO_ROOT/src/network/wifi_text_codec.c"
    "$DEMO_ROOT/src/network/network_failover.c"
    "$DEMO_ROOT/third_party/paho-embedded-c/MQTTClient-C/src/MQTTClient.c"
    "$DEMO_ROOT/third_party/paho-embedded-c/MQTTPacket/src/MQTTConnectClient.c"
    "$DEMO_ROOT/third_party/paho-embedded-c/MQTTPacket/src/MQTTDeserializePublish.c"
    "$DEMO_ROOT/third_party/paho-embedded-c/MQTTPacket/src/MQTTFormat.c"
    "$DEMO_ROOT/third_party/paho-embedded-c/MQTTPacket/src/MQTTPacket.c"
    "$DEMO_ROOT/third_party/paho-embedded-c/MQTTPacket/src/MQTTSerializePublish.c"
    "$DEMO_ROOT/third_party/paho-embedded-c/MQTTPacket/src/MQTTSubscribeClient.c"
    "$DEMO_ROOT/third_party/paho-embedded-c/MQTTPacket/src/MQTTUnsubscribeClient.c"
)

# 保存全部 Demo 目标文件。
DEMO_OBJECTS=()

# 使用 C11 和严格告警逐模块编译 Demo。
OBJECT_INDEX=0
for SOURCE in "${DEMO_SOURCES[@]}"; do
    # 自有代码保持零告警强制，未修改的 Paho 上游源码保留其原始编译告警。
    SOURCE_WARNING_FLAGS=(-Werror)
    if [[ "$SOURCE" == "$DEMO_ROOT/third_party/"* ]]; then
        SOURCE_WARNING_FLAGS=(-Wno-error)
    fi

    OBJECT="$BUILD_ROOT/obj/${OBJECT_INDEX}_$(basename "${SOURCE%.c}").o"
    "${TOOLCHAIN_PREFIX}gcc" \
        -std=c11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L \
        -DMQTTCLIENT_PLATFORM_HEADER=mqtt_tls_platform.h \
        -O2 -Wall -Wextra "${SOURCE_WARNING_FLAGS[@]}" \
        -include "$DEMO_ROOT/src/mqtt/internal/mqtt_tls_platform.h" \
        -I"$SDK_ROOT/include" \
        -I"$SDK_ROOT/runtime/include" \
        -I"$DEMO_ROOT/src" \
        -I"$DEMO_ROOT/src/cloud" \
        -I"$DEMO_ROOT/src/cloud/internal" \
        -I"$DEMO_ROOT/src/mqtt" \
        -I"$DEMO_ROOT/src/mqtt/internal" \
        -I"$DEMO_ROOT/third_party/paho-embedded-c/MQTTClient-C/src" \
        -I"$DEMO_ROOT/third_party/paho-embedded-c/MQTTPacket/src" \
        -c "$SOURCE" -o "$OBJECT"
    DEMO_OBJECTS+=("$OBJECT")
    OBJECT_INDEX=$((OBJECT_INDEX + 1))
done

# 链接客户 SDK、TLS 和线程运行库。
"${TOOLCHAIN_PREFIX}gcc" \
    "${DEMO_OBJECTS[@]}" \
    -o "$BUILD_ROOT/bin/workcard-demo" \
    -L"$SDK_ROOT/lib/arm-linux-musleabi" \
    -L"$SDK_ROOT/runtime/lib" \
    -Wl,-rpath-link="$SDK_ROOT/runtime/lib" \
    -lworkcard_sdk -lssl -lcrypto -lpthread -ldl -lm

# 删除 Demo 中不需要部署的符号和调试段。
"${TOOLCHAIN_PREFIX}strip" --strip-unneeded "$BUILD_ROOT/bin/workcard-demo"

# 创建正式 Demo 二进制交付目录。
mkdir -p "$DEMO_ROOT/bin/arm-linux-musleabi"

# 保存同目录临时文件，兼容 WSL 下不允许 chmod 的 Windows NTFS 挂载目录。
DELIVERY_TEMP="$DEMO_ROOT/bin/arm-linux-musleabi/workcard-demo.tmp"

# 复制链接器已经生成的可执行文件，不额外修改 Windows 文件权限。
cp "$BUILD_ROOT/bin/workcard-demo" "$DELIVERY_TEMP"

# 在同一目录原子替换正式 Demo，避免客户看到半截二进制。
mv -f "$DELIVERY_TEMP" "$DEMO_ROOT/bin/arm-linux-musleabi/workcard-demo"

# 输出目标架构和大小供客户检查。
file "$BUILD_ROOT/bin/workcard-demo"
ls -lh "$BUILD_ROOT/bin/workcard-demo"
