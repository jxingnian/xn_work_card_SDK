#!/bin/sh

# 任一安装步骤失败时立即停止。
set -eu

# 解析当前脚本所在的 SDK 根目录。
SDK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# 固定目标板 SDK 安装目录。
TARGET_ROOT="/opt/workcard-sdk"

# 固定目标板网络基础组件目录。
CONNECTIVITY_ROOT="/opt/workcard-connectivity"

# 固定必联 RTL8723DS 蓝牙运行组件目录。
BLUETOOTH_RUNTIME_ROOT="/opt/workcard-bluetooth/runtime"

# 固定核心板引脚复用模块的目标路径。
BOARD_SYS_CONFIG_MODULE="/komod/sys_config.ko"

# 固定安装前原始引脚复用模块的可回滚备份路径。
BOARD_SYS_CONFIG_BACKUP="/komod/sys_config.ko.workcard-backup"

# 固定目标内核版本，驱动模块必须与该版本严格匹配。
TARGET_KERNEL_VERSION="5.10.221"

# 要求在固定 rootfs 的 root 用户下执行安装。
if [ "$(id -u)" -ne 0 ]; then
    echo "安装失败: 必须在目标板使用 root 用户执行" >&2
    exit 77
fi

# 判断指定 PID 文件对应进程是否正在运行。
pid_file_is_running()
{
    # PID 文件不可读时返回未运行。
    [ -r "$1" ] || return 1

    # 读取并校验进程仍存在。
    RUNNING_PID="$(cat "$1" 2>/dev/null)"
    [ -n "$RUNNING_PID" ] && kill -0 "$RUNNING_PID" 2>/dev/null
}

# 拒绝与旧云端媒体程序共同占用 HAL，安装脚本不擅自删除客户系统服务。
if pid_file_is_running /run/lite-p-mqtt-agent.pid ||
   pid_file_is_running /run/lite-p-device-stream.pid ||
   pid_file_is_running /run/lite-p-stream-worker.pid; then
    echo "安装失败: 旧 lite-p MQTT/媒体服务仍在运行，请先停止并在客户 SDK 固件中禁用旧服务" >&2
    exit 69
fi

# 校验必要二进制和配置完整存在。
for REQUIRED_FILE in \
    "$SDK_ROOT/bin/arm-linux-musleabi/workcard-sdk-service" \
    "$SDK_ROOT/lib/arm-linux-musleabi/libworkcard_sdk.so.1.0.0" \
    "$SDK_ROOT/runtime/config/hal_default.json" \
    "$SDK_ROOT/runtime/config/sc2356_stream.json" \
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
    "$SDK_ROOT/runtime/connectivity/udhcpc-eth0-local-only" \
    "$SDK_ROOT/runtime/connectivity/ws73-wifi-worker" \
    "$SDK_ROOT/runtime/connectivity/wifi.conf.example" \
    "$SDK_ROOT/runtime/service/S91workcard_sdk" \
    "$SDK_ROOT/runtime/service/workcard-sdk-supervisor"; do
    if [ ! -f "$REQUIRED_FILE" ]; then
        echo "安装失败: 缺少 $REQUIRED_FILE" >&2
        exit 66
    fi
done

# 固定固件只允许部署到与驱动匹配的内核版本。
if [ "$(uname -r)" != "$TARGET_KERNEL_VERSION" ]; then
    echo "安装失败: 当前内核 $(uname -r) 与固定版本 $TARGET_KERNEL_VERSION 不匹配" >&2
    exit 65
fi

# 核心板必须提供正式模块目录，禁止把板级驱动安装到不匹配的 rootfs。
if [ ! -d /komod ] || [ ! -f "$BOARD_SYS_CONFIG_MODULE" ]; then
    echo "安装失败: 固定 rootfs 缺少 $BOARD_SYS_CONFIG_MODULE" >&2
    exit 66
fi

# 必联蓝牙硬件必须连接在历史设计确认的 UART1。
if [ ! -c /dev/ttyAMA1 ]; then
    echo "安装失败: 缺少必联 RTL8723DS 使用的 /dev/ttyAMA1" >&2
    exit 65
fi

# 正式基线禁止预先加载 WS73 自带蓝牙模块。
if [ -d /sys/module/ble_soc ]; then
    echo "安装失败: 当前系统已加载禁止使用的 WS73 ble_soc，请先停止旧蓝牙服务并重启" >&2
    exit 69
fi

# 使用 ELF 文件头拒绝脚本或损坏文件，详细 ARM 架构由交付校验在开发机完成。
if [ "$(dd if="$SDK_ROOT/runtime/bluetooth/bin/rtk_hciattach" bs=4 count=1 2>/dev/null)" != "$(printf '\177ELF')" ]; then
    echo "安装失败: rtk_hciattach 不是有效 ELF 程序" >&2
    exit 65
fi

# 正式客户基线不得残留旧 MQTT、媒体或旧路径 WiFi 自启动脚本。
for LEGACY_SERVICE in \
    /etc/init.d/S90autorun.pre-sc2356 \
    /etc/init.d/S93xingnian_wifi \
    /etc/init.d/S94lite_p_mqtt \
    /etc/init.d/S95lite_p_stream; do
    if [ -e "$LEGACY_SERVICE" ]; then
        echo "安装失败: 固定固件仍包含旧服务 $LEGACY_SERVICE，请先按部署文档清理基线" >&2
        exit 69
    fi
done

# 停止旧版服务，避免替换正在执行的二进制。
if [ -f /run/workcard-demo.pid ]; then
    # 先正常停止客户 Demo，释放其 SDK 会话和摄像头资源。
    DEMO_PID="$(cat /run/workcard-demo.pid 2>/dev/null || true)"
    if [ -n "$DEMO_PID" ] && kill -0 "$DEMO_PID" 2>/dev/null; then
        kill -TERM "$DEMO_PID" 2>/dev/null || true
        WAITED_SECONDS="0"
        while kill -0 "$DEMO_PID" 2>/dev/null && [ "$WAITED_SECONDS" -lt 15 ]; do
            sleep 1
            WAITED_SECONDS=$((WAITED_SECONDS + 1))
        done
    fi
    rm -f /run/workcard-demo.pid
fi

# 停止旧版底层服务，避免替换正在执行的二进制。
if [ -x /etc/init.d/S91workcard_sdk ]; then
    /etc/init.d/S91workcard_sdk stop || true
fi

# 创建权限确定的目标目录和仅 root 可访问的状态目录。
install -d -m 0755 "$TARGET_ROOT/bin" "$TARGET_ROOT/lib" "$TARGET_ROOT/config"
install -d -m 0700 "$TARGET_ROOT/state"

# 创建独立于旧业务程序的系统联网组件目录。
install -d -m 0755 "$CONNECTIVITY_ROOT"

# 创建必联蓝牙程序、驱动和固件目录。
install -d -m 0755 \
    "$BLUETOOTH_RUNTIME_ROOT" \
    "/lib/modules/$TARGET_KERNEL_VERSION/extra" \
    "/lib/firmware/rtlbt" \
    "/etc/bluetooth"

# 安装已 strip 的底层服务。
install -m 0755 \
    "$SDK_ROOT/bin/arm-linux-musleabi/workcard-sdk-service" \
    "$TARGET_ROOT/bin/workcard-sdk-service"

# 安装客户动态库并提供 ABI 主版本名称。
install -m 0755 \
    "$SDK_ROOT/lib/arm-linux-musleabi/libworkcard_sdk.so.1.0.0" \
    "$TARGET_ROOT/lib/libworkcard_sdk.so.1.0.0"
cp "$TARGET_ROOT/lib/libworkcard_sdk.so.1.0.0" "$TARGET_ROOT/lib/libworkcard_sdk.so.1"
cp "$TARGET_ROOT/lib/libworkcard_sdk.so.1.0.0" "$TARGET_ROOT/lib/libworkcard_sdk.so"
chmod 0755 "$TARGET_ROOT/lib/libworkcard_sdk.so.1" "$TARGET_ROOT/lib/libworkcard_sdk.so"

# 安装经过 ELF 依赖裁剪的全部私有运行库。
for LIBRARY in "$SDK_ROOT"/runtime/lib/*; do
    if [ -f "$LIBRARY" ]; then
        install -m 0755 "$LIBRARY" "$TARGET_ROOT/lib/$(basename "$LIBRARY")"
    fi
done

# 安装当前 SC2356 和 HAL 配置。
install -m 0644 "$SDK_ROOT/runtime/config/hal_default.json" "$TARGET_ROOT/config/hal_default.json"
install -m 0644 "$SDK_ROOT/runtime/config/sc2356_stream.json" "$TARGET_ROOT/config/sc2356_stream.json"

# 安装固定以太网和 WS73 联网守护组件，但不覆盖目标板已有真实 WiFi 凭据。
install -m 0755 "$SDK_ROOT/runtime/connectivity/udhcpc-eth0-local-only" "$CONNECTIVITY_ROOT/udhcpc-eth0-local-only"
install -m 0755 "$SDK_ROOT/runtime/connectivity/ws73-wifi-worker" "$CONNECTIVITY_ROOT/ws73-wifi-worker"
install -m 0600 "$SDK_ROOT/runtime/connectivity/wifi.conf.example" "$CONNECTIVITY_ROOT/wifi.conf.example"
install -m 0644 "$SDK_ROOT/runtime/connectivity/dbus-system.conf" "$CONNECTIVITY_ROOT/dbus-system.conf"

# 安装与固定内核匹配的必联 RTL8723DS UART 蓝牙组件。
install -m 0755 "$SDK_ROOT/runtime/bluetooth/bin/rtk_hciattach" /usr/sbin/rtk_hciattach
install -m 0644 "$SDK_ROOT/runtime/bluetooth/modules/hci_uart.ko" "/lib/modules/$TARGET_KERNEL_VERSION/extra/hci_uart.ko"

# 首次安装时保存客户设备原始板级模块，重复升级不得覆盖恢复基线。
if [ ! -e "$BOARD_SYS_CONFIG_BACKUP" ]; then
    cp -p "$BOARD_SYS_CONFIG_MODULE" "$BOARD_SYS_CONFIG_BACKUP"
fi

# 原子安装包含 UART1 CTS/RTS/RX/TX 引脚复用的闭源板级模块。
install -m 0644 "$SDK_ROOT/runtime/board/modules/sys_config.ko" "$BOARD_SYS_CONFIG_MODULE.new"
mv -f "$BOARD_SYS_CONFIG_MODULE.new" "$BOARD_SYS_CONFIG_MODULE"

# 持久化模块替换，确保下一次整机启动使用新引脚配置。
sync
install -m 0644 "$SDK_ROOT/runtime/bluetooth/firmware/rtl8723d_fw" /lib/firmware/rtlbt/rtl8723d_fw
install -m 0644 "$SDK_ROOT/runtime/bluetooth/firmware/rtl8723d_config" /lib/firmware/rtlbt/rtl8723d_config
install -m 0644 "$SDK_ROOT/runtime/connectivity/rtl8723ds.conf" /etc/bluetooth/rtl8723ds.conf

# 首次安装时保存固定 rootfs 的原始 BlueZ ELF，供私有运行库包装器调用。
if [ ! -e /opt/bluealsa/bin/bluetoothctl.bin ]; then
    # 原始系统命令必须存在且可执行。
    if [ ! -x /usr/bin/bluetoothctl ]; then
        echo "安装失败: 缺少固定 rootfs 的 /usr/bin/bluetoothctl" >&2
        exit 66
    fi

    # 拒绝把旧包装脚本当作真实 BlueZ 程序保存。
    if [ "$(dd if=/usr/bin/bluetoothctl bs=4 count=1 2>/dev/null)" != "$(printf '\177ELF')" ]; then
        echo "安装失败: /usr/bin/bluetoothctl 不是原始 ELF，且私有真实程序不存在" >&2
        exit 65
    fi

    # 保存与固定 rootfs 匹配的真实 BlueZ 命令。
    install -m 0755 /usr/bin/bluetoothctl /opt/bluealsa/bin/bluetoothctl.bin
fi

# 安装只对 bluetoothctl 设置私有 BlueZ/D-Bus 运行库的命令包装器。
install -m 0755 "$SDK_ROOT/runtime/connectivity/bluetoothctl" /usr/bin/bluetoothctl

# 安装无 Telnet 网络基线，并按独立模组顺序启动星闪 WiFi 和必联蓝牙。
install -m 0755 "$SDK_ROOT/runtime/connectivity/S80network" /etc/init.d/S80network
install -m 0755 "$SDK_ROOT/runtime/connectivity/S89eth0_dhcp" /etc/init.d/S89eth0_dhcp
install -m 0755 "$SDK_ROOT/runtime/connectivity/S92workcard_wifi" /etc/init.d/S92workcard_wifi
install -m 0755 "$SDK_ROOT/runtime/connectivity/S93bluetooth_audio" /etc/init.d/S93bluetooth_audio
rm -f /etc/init.d/S92bluetooth_audio /etc/init.d/S93workcard_wifi

# 删除错误路线遗留的旧蓝牙配置，避免脚本读取 WS73 参数。
rm -f /etc/bluetooth/bluetooth_audio.conf

# 安装并启动 SysV 底层服务。
install -m 0755 "$SDK_ROOT/runtime/service/workcard-sdk-supervisor" "$TARGET_ROOT/bin/workcard-sdk-supervisor"
install -m 0755 "$SDK_ROOT/runtime/service/S91workcard_sdk" /etc/init.d/S91workcard_sdk
/etc/init.d/S91workcard_sdk start

# 输出不包含客户凭据的安装状态。
/etc/init.d/S91workcard_sdk status

# 板级模块只能在下次整机启动时生效，安装脚本不冒险卸载正在运行的系统模块。
echo "安装完成: 必须整机重启后再执行蓝牙验收"
