#!/usr/bin/env bash

# 任一交付检查失败时立即停止。
set -euo pipefail

# 解析客户 SDK 根目录。
SDK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# 校验关键目录和文件完整存在。
for REQUIRED_PATH in \
    "$SDK_ROOT/include/workcard/workcard_sdk.h" \
    "$SDK_ROOT/lib/arm-linux-musleabi/libworkcard_sdk.so.1.0.0" \
    "$SDK_ROOT/bin/arm-linux-musleabi/workcard-sdk-service" \
    "$SDK_ROOT/bin/arm-linux-musleabi/workcard-bluetooth-agent" \
    "$SDK_ROOT/demo/src/main.c" \
    "$SDK_ROOT/demo/src/mqtt/mqtt_demo.c" \
    "$SDK_ROOT/demo/src/mqtt/internal/desired_config_parser.c" \
    "$SDK_ROOT/demo/src/mqtt/internal/mqtt_tls_platform.c" \
    "$SDK_ROOT/demo/src/mqtt/internal/wifi_command_parser.c" \
    "$SDK_ROOT/demo/src/network/network_failover.c" \
    "$SDK_ROOT/demo/src/network/wifi_profile_store.c" \
    "$SDK_ROOT/demo/src/network/wifi_text_codec.c" \
    "$SDK_ROOT/demo/tests/test_wifi_command_parser.c" \
    "$SDK_ROOT/demo/tests/test_wifi_text_codec.c" \
    "$SDK_ROOT/demo/third_party/paho-embedded-c/epl-v10" \
    "$SDK_ROOT/demo/third_party/paho-embedded-c/edl-v10" \
    "$SDK_ROOT/demo/config/ca-certificates.crt" \
    "$SDK_ROOT/scripts/setup_dev_env.sh" \
    "$SDK_ROOT/scripts/toolchain_env.sh" \
    "$SDK_ROOT/scripts/create_target_bundle.sh" \
    "$SDK_ROOT/scripts/clean_legacy_target.sh" \
    "$SDK_ROOT/runtime/connectivity/S80network" \
    "$SDK_ROOT/runtime/connectivity/S89eth0_dhcp" \
    "$SDK_ROOT/runtime/connectivity/S93bluetooth_audio" \
    "$SDK_ROOT/runtime/connectivity/S92workcard_wifi" \
    "$SDK_ROOT/runtime/connectivity/rtl8723ds.conf" \
    "$SDK_ROOT/runtime/bluetooth/bin/rtk_hciattach" \
    "$SDK_ROOT/runtime/bluetooth/modules/hci_uart.ko" \
    "$SDK_ROOT/runtime/board/modules/sys_config.ko" \
    "$SDK_ROOT/runtime/bluetooth/firmware/rtl8723d_fw" \
    "$SDK_ROOT/runtime/bluetooth/firmware/rtl8723d_config" \
    "$SDK_ROOT/runtime/connectivity/bluetoothctl" \
    "$SDK_ROOT/runtime/connectivity/dbus-system.conf" \
    "$SDK_ROOT/runtime/connectivity/ws73-wifi-worker" \
    "$SDK_ROOT/runtime/service/workcard-sdk-supervisor" \
    "$SDK_ROOT/runtime/service/S94workcard_demo" \
    "$SDK_ROOT/runtime/service/S95workcard_bluetooth" \
    "$SDK_ROOT/runtime/connectivity/bluetooth-scan-worker" \
    "$SDK_ROOT/runtime/connectivity/bluetooth-action-worker" \
    "$SDK_ROOT/demo/config/bluetooth-test.wav" \
    "$SDK_ROOT/demo/src/bluetooth/bluetooth_demo.c" \
    "$SDK_ROOT/demo/src/bluetooth/bluetooth_demo.h" \
    "$SDK_ROOT/scripts/verify_target_bluetooth.sh" \
    "$SDK_ROOT/dist/workcard-target-1.0.0.tar.gz" \
    "$SDK_ROOT/dist/workcard-target-1.0.0.tar.gz.sha256" \
    "$SDK_ROOT/toolchain/packages/workcard-arm-linux-musleabi-toolchain-20250305.tar.gz" \
    "$SDK_ROOT/toolchain/packages/workcard-arm-linux-musleabi-toolchain-20250305.tar.gz.sha256" \
    "$SDK_ROOT/doc/API" \
    "$SDK_ROOT/doc/部署/09_后台动态摄像头配置.md"; do
    if [ ! -e "$REQUIRED_PATH" ]; then
        echo "交付检查失败: 缺少 $REQUIRED_PATH" >&2
        exit 66
    fi
done

# 校验必联初始化工具为已裁剪的三十二位 ARM 程序。
file "$SDK_ROOT/runtime/bluetooth/bin/rtk_hciattach" | grep -q 'ELF 32-bit.*ARM.*stripped'

# 校验蓝牙模块声明的固定内核版本。
modinfo -F vermagic "$SDK_ROOT/runtime/bluetooth/modules/hci_uart.ko" | grep -q '^5\.10\.221'

# 校验板级引脚复用模块声明的固定内核版本。
modinfo -F vermagic "$SDK_ROOT/runtime/board/modules/sys_config.ko" | grep -q '^5\.10\.221'

# 禁止正式蓝牙服务再包含 WS73 蓝牙加载逻辑。
if grep -En 'BLE_SOC_MODULE|start_ws73_hci|insmod[^[:space:]]*ble_soc' "$SDK_ROOT/runtime/connectivity/S93bluetooth_audio"; then
    echo "交付检查失败: 仍包含禁止使用的 WS73 蓝牙逻辑" >&2
    exit 65
fi

# 禁止客户 SDK 运行目录携带 WS73 蓝牙模块文件。
if find "$SDK_ROOT/runtime" -type f -name 'ble_soc.ko' -print -quit | grep -q .; then
    echo "交付检查失败: 运行包包含禁止使用的 ble_soc.ko" >&2
    exit 65
fi

# 禁止把必联模组的 SDIO WiFi 驱动放入目标运行包。
if find "$SDK_ROOT/runtime" -type f \( -iname '*8723ds*wifi*' -o -iname '8723ds.ko' \) -print -quit | grep -q .; then
    echo "交付检查失败: 运行包包含禁止使用的 RTL8723DS WiFi 驱动" >&2
    exit 65
fi

# 在解压前校验随附工具链归档的独立 SHA-256。
(
    # 切换到独立校验文件所在目录。
    cd "$SDK_ROOT/toolchain/packages"

    # 校验固定工具链归档没有传输损坏或被替换。
    sha256sum -c workcard-arm-linux-musleabi-toolchain-20250305.tar.gz.sha256
)

# 校验目标板精简包独立 SHA-256。
(
    # 切换到目标板精简包输出目录。
    cd "$SDK_ROOT/dist"

    # 校验上传交付使用的固定版本精简包。
    sha256sum -c workcard-target-1.0.0.tar.gz.sha256
)

# 一次读取目标板精简包目录，避免重复解压目录索引和管道提前退出。
TARGET_CONTENTS="$(tar -tzf "$SDK_ROOT/dist/workcard-target-1.0.0.tar.gz")"

# 拒绝把 x86 客户工具链放入小容量 ARM 目标板精简包。
if grep -q '^work_card_SDK/toolchain/' <<<"$TARGET_CONTENTS"; then
    echo "交付检查失败: 目标板精简包包含 x86 工具链" >&2
    exit 65
fi

# 目标板精简包必须包含底层服务、Demo、CA 和旧基线清理脚本。
for TARGET_PATH in \
    work_card_SDK/bin/arm-linux-musleabi/workcard-sdk-service \
    work_card_SDK/demo/bin/arm-linux-musleabi/workcard-demo \
    work_card_SDK/demo/config/ca-certificates.crt \
    work_card_SDK/runtime/service/workcard-sdk-supervisor \
    work_card_SDK/runtime/service/S94workcard_demo \
    work_card_SDK/runtime/bluetooth/bin/rtk_hciattach \
    work_card_SDK/runtime/bluetooth/modules/hci_uart.ko \
    work_card_SDK/runtime/board/modules/sys_config.ko \
    work_card_SDK/runtime/bluetooth/firmware/rtl8723d_fw \
    work_card_SDK/runtime/bluetooth/firmware/rtl8723d_config \
    work_card_SDK/scripts/verify_target_bluetooth.sh \
    work_card_SDK/scripts/clean_legacy_target.sh; do
    if ! grep -Fxq "$TARGET_PATH" <<<"$TARGET_CONTENTS"; then
        echo "交付检查失败: 目标板精简包缺少 $TARGET_PATH" >&2
        exit 66
    fi
done

# 校验交付二进制目标架构且不包含调试符号。
file "$SDK_ROOT/lib/arm-linux-musleabi/libworkcard_sdk.so.1.0.0"
file "$SDK_ROOT/bin/arm-linux-musleabi/workcard-sdk-service"
file "$SDK_ROOT/lib/arm-linux-musleabi/libworkcard_sdk.so.1.0.0" | grep -q 'ELF 32-bit.*ARM.*stripped'
file "$SDK_ROOT/bin/arm-linux-musleabi/workcard-sdk-service" | grep -q 'ELF 32-bit.*ARM.*stripped'

# 校验客户库没有导出非 workcard_ 业务符号。
UNEXPECTED_EXPORTS="$(nm -D --defined-only \
    "$SDK_ROOT/lib/arm-linux-musleabi/libworkcard_sdk.so.1.0.0" | \
    awk '{print $3}' | grep -Ev '^(WORKCARD_SDK_1\.0|workcard_)' || true)"
if [ -n "$UNEXPECTED_EXPORTS" ]; then
    echo "交付检查失败: 客户库存在非公开导出符号" >&2
    echo "$UNEXPECTED_EXPORTS" >&2
    exit 65
fi

# 校验客户目录没有混入 SDK 私有实现目录。
if find "$SDK_ROOT" -type f \( -name 'workcard_service.c' -o -name 'workcard_media_backend.c' \) | grep -q .; then
    echo "交付检查失败: 客户目录包含底层服务私有源码" >&2
    exit 65
fi

# 输出通过检查的明确结果。
echo "work_card_SDK 交付结构检查通过"
