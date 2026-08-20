#!/bin/sh

# 任一未处理命令失败时立即结束验收。
set -eu

# 固定正式蓝牙服务入口。
BLUETOOTH_SERVICE="/etc/init.d/S93bluetooth_audio"

# 固定包含 UART1 CTS/RTS/RX/TX 四线复用的板级模块哈希。
BOARD_SYS_CONFIG_SHA256="0a64226a63ea72b9210a59d2a51ed805396fb69dd3f88ebcf2c71650ad050eed"

# 校验目标板基础运行文件完整。
for REQUIRED_PATH in \
    /dev/ttyAMA1 \
    /komod/sys_config.ko \
    /lib/modules/5.10.221/extra/hci_uart.ko \
    /usr/sbin/rtk_hciattach \
    /lib/firmware/rtlbt/rtl8723d_fw \
    /lib/firmware/rtlbt/rtl8723d_config \
    "$BLUETOOTH_SERVICE"; do
    # 缺少任一文件时输出精确路径并停止。
    if [ ! -e "$REQUIRED_PATH" ]; then
        echo "蓝牙验收失败: 缺少 $REQUIRED_PATH" >&2
        exit 66
    fi
done

# 拒绝使用缺少核心板 33 脚 CTS 配置的旧板级模块。
if [ "$(sha256sum /komod/sys_config.ko | awk '{print $1}')" != "$BOARD_SYS_CONFIG_SHA256" ]; then
    echo "蓝牙验收失败: sys_config.ko 不是 UART1 四线硬件流控版本" >&2
    exit 65
fi

# 禁止星闪蓝牙模块与必联经典蓝牙同时运行。
if [ -d /sys/module/ble_soc ]; then
    echo "蓝牙验收失败: WS73 ble_soc 已加载" >&2
    exit 65
fi

# 启动完整 UART HCI、BlueZ 和 BlueALSA 服务。
if ! "$BLUETOOTH_SERVICE" restart; then
    # 输出 UART1 驱动累计的帧错和奇偶错计数。
    grep '^1:' /proc/tty/driver/ttyAMA 2>/dev/null || true
    # 输出 Realtek H5 初始化日志供定位握手阶段。
    tail -n 80 /tmp/bluetooth-audio.log 2>/dev/null || true
    echo "蓝牙验收失败: RTL8723DS H5 初始化未完成" >&2
    exit 1
fi

# 要求控制器必须由 UART 线路规程注册为 hci0。
if [ ! -d /sys/class/bluetooth/hci0 ] || [ ! -d /sys/module/hci_uart ]; then
    echo "蓝牙验收失败: hci0 或 hci_uart 不存在" >&2
    exit 1
fi

# 要求三个正式蓝牙进程同时存在。
for PROCESS_NAME in rtk_hciattach bluetoothd bluealsad; do
    # 使用 pidof 避免把 grep 自身误判为服务进程。
    if ! pidof "$PROCESS_NAME" >/dev/null 2>&1; then
        echo "蓝牙验收失败: $PROCESS_NAME 未运行" >&2
        exit 1
    fi
done

# 要求 WiFi 继续使用 WS73 平台驱动且没有被蓝牙改动破坏。
if [ ! -d /sys/module/plat_soc ] || [ ! -d /sys/module/wifi_soc ]; then
    echo "蓝牙验收失败: WS73 WiFi 驱动未运行" >&2
    exit 1
fi

# 输出最终服务状态和 UART 统计供交付记录留档。
"$BLUETOOTH_SERVICE" status
grep '^1:' /proc/tty/driver/ttyAMA 2>/dev/null || true
echo "蓝牙验收通过: WS73 仅 WiFi，RTL8723DS 经典蓝牙运行正常"
