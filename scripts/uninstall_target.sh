#!/bin/sh

# 任一卸载步骤失败时立即停止。
set -eu

# 要求在固定 rootfs 的 root 用户下执行卸载。
if [ "$(id -u)" -ne 0 ]; then
    echo "卸载失败: 必须在目标板使用 root 用户执行" >&2
    exit 77
fi

# 先正常停止底层服务并释放摄像头和音频资源。
if [ -x /etc/init.d/S95workcard_bluetooth ]; then
    # 先停止应用层蓝牙自动连接代理。
    /etc/init.d/S95workcard_bluetooth stop || true
fi

if [ -x /etc/init.d/S93bluetooth_audio ]; then
    # 再停止 BlueALSA、BlueZ 和 RTL8723DS UART 线路规程。
    /etc/init.d/S93bluetooth_audio stop || {
        echo "卸载失败: 必联蓝牙服务未能正常停止，已保留现场" >&2
        exit 70
    }
fi

if [ -x /etc/init.d/S92workcard_wifi ]; then
    # 停止星闪 WiFi 联网守护。
    /etc/init.d/S92workcard_wifi stop || true
fi

if [ -x /etc/init.d/S91workcard_sdk ]; then
    /etc/init.d/S91workcard_sdk stop || true
fi

# 删除 SDK 自身安装目录和服务脚本，不删除客户应用、凭据或其他系统服务。
rm -rf /opt/workcard-sdk
rm -f /etc/init.d/S91workcard_sdk
rm -f /etc/init.d/S92workcard_wifi /etc/init.d/S93bluetooth_audio /etc/init.d/S95workcard_bluetooth
rm -f /usr/sbin/rtk_hciattach
rm -f /lib/modules/5.10.221/extra/hci_uart.ko
rm -f /lib/firmware/rtlbt/rtl8723d_fw /lib/firmware/rtlbt/rtl8723d_config
rm -f /etc/bluetooth/rtl8723ds.conf
rm -f /run/workcard-sdk/service.sock /run/workcard-sdk-service.pid
rm -f /run/rtk_hciattach.pid /run/bluetoothd.pid /run/bluealsad.pid

# 删除 SDK 管理的运行组件，但保留客户 WiFi、蓝牙目标和 MQTT 状态数据。
rm -rf /opt/workcard-connectivity

# 存在安装前板级模块备份时原子恢复，避免卸载后遗留 SDK 私有引脚配置。
if [ -f /komod/sys_config.ko.workcard-backup ]; then
    cp -p /komod/sys_config.ko.workcard-backup /komod/sys_config.ko.restore
    mv -f /komod/sys_config.ko.restore /komod/sys_config.ko
    rm -f /komod/sys_config.ko.workcard-backup
    sync
    echo "卸载完成: 已恢复原板级模块，必须整机重启后生效"
fi
