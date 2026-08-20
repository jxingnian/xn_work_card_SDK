#!/bin/sh

# 任一基线校验、网络迁移或旧业务清理失败时立即停止。
set -eu

# 解析当前目标板精简包中的 SDK 根目录。
SDK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# 固定新网络基础组件目录。
CONNECTIVITY_ROOT="/opt/workcard-connectivity"

# 固定旧业务目录，禁止通过参数扩大删除范围。
LEGACY_ROOT="/opt/lite-p-stream"

# 清理属于不可恢复操作，要求发布人员显式确认准确值。
if [ "${CONFIRM_CLEAN_LEGACY:-}" != "YES" ]; then
    echo "旧基线清理失败: 必须设置 CONFIRM_CLEAN_LEGACY=YES" >&2
    exit 64
fi

# 只允许目标板 root 用户执行系统级清理。
if [ "$(id -u)" -ne 0 ]; then
    echo "旧基线清理失败: 必须在目标板使用 root 用户执行" >&2
    exit 77
fi

# 只允许当前三十二位 ARM 固定硬件执行，防止误清理开发机。
if [ "$(uname -m)" != "armv7l" ]; then
    echo "旧基线清理失败: 当前设备不是固定 ARMv7 目标板" >&2
    exit 65
fi

# 只允许当前 UBI rootfs 基线执行，防止误清理其他 ARM Linux。
if ! grep -q 'root=ubi0:ubifs' /proc/cmdline; then
    echo "旧基线清理失败: 当前设备不是固定 UBIFS rootfs" >&2
    exit 65
fi

# 校验新网络基础组件全部来自当前 SDK 精简包。
for REQUIRED_FILE in \
    "$SDK_ROOT/runtime/connectivity/S89eth0_dhcp" \
    "$SDK_ROOT/runtime/connectivity/S92workcard_wifi" \
    "$SDK_ROOT/runtime/connectivity/udhcpc-eth0-local-only" \
    "$SDK_ROOT/runtime/connectivity/ws73-wifi-worker"; do
    if [ ! -f "$REQUIRED_FILE" ]; then
        echo "旧基线清理失败: 缺少 $REQUIRED_FILE" >&2
        exit 66
    fi
done

# 在删除旧目录前创建独立网络基础组件目录。
install -d -m 0755 "$CONNECTIVITY_ROOT"

# 迁移当前实验板的 WiFi 凭据以保持 SSH，正式客户固件应通过生产配置流程写入。
if [ -r "$LEGACY_ROOT/wifi.conf" ] && [ ! -e "$CONNECTIVITY_ROOT/wifi.conf" ]; then
    install -m 0600 "$LEGACY_ROOT/wifi.conf" "$CONNECTIVITY_ROOT/wifi.conf"
fi

# 安装不依赖旧业务目录的固定网络脚本。
install -m 0755 "$SDK_ROOT/runtime/connectivity/udhcpc-eth0-local-only" "$CONNECTIVITY_ROOT/udhcpc-eth0-local-only"
install -m 0755 "$SDK_ROOT/runtime/connectivity/ws73-wifi-worker" "$CONNECTIVITY_ROOT/ws73-wifi-worker"
install -m 0755 "$SDK_ROOT/runtime/connectivity/S89eth0_dhcp" /etc/init.d/S89eth0_dhcp
install -m 0755 "$SDK_ROOT/runtime/connectivity/S92workcard_wifi" /etc/init.d/S92workcard_wifi

# 只在旧 WiFi 服务存在时执行一次无旧路径切换。
if [ -x /etc/init.d/S93xingnian_wifi ]; then
    # 停止旧联网守护及其用户态客户端。
    /etc/init.d/S93xingnian_wifi stop || true

    # 已迁移真实 WiFi 配置时立即启动新联网守护。
    if [ -r "$CONNECTIVITY_ROOT/wifi.conf" ]; then
        /etc/init.d/S92workcard_wifi start

        # 最多等待十秒确认新守护进程正常存在。
        WAITED_SECONDS="0"
        while [ "$WAITED_SECONDS" -lt 10 ]; do
            if [ -r /run/xingnian-wifi.pid ]; then
                WIFI_PID="$(cat /run/xingnian-wifi.pid 2>/dev/null)"
                if [ -n "$WIFI_PID" ] && kill -0 "$WIFI_PID" 2>/dev/null; then
                    break
                fi
            fi
            sleep 1
            WAITED_SECONDS=$((WAITED_SECONDS + 1))
        done

        # 新守护未启动时恢复旧服务并拒绝删除旧目录。
        if [ ! -r /run/xingnian-wifi.pid ] || ! kill -0 "$(cat /run/xingnian-wifi.pid 2>/dev/null)" 2>/dev/null; then
            /etc/init.d/S93xingnian_wifi start || true
            echo "旧基线清理失败: 新 WiFi 守护未启动，已经尝试恢复旧服务" >&2
            exit 69
        fi
    fi
fi

# 正常停止旧 MQTT 和媒体服务，避免删除正在执行的二进制。
for LEGACY_SERVICE in /etc/init.d/S94lite_p_mqtt /etc/init.d/S95lite_p_stream; do
    if [ -x "$LEGACY_SERVICE" ]; then
        "$LEGACY_SERVICE" stop || true
    fi
done

# 根据固定 PID 文件停止残留旧业务进程。
for PID_FILE in \
    /run/lite-p-mqtt-agent.pid \
    /run/lite-p-device-stream.pid \
    /run/lite-p-stream-worker.pid; do
    if [ -r "$PID_FILE" ]; then
        LEGACY_PID="$(cat "$PID_FILE" 2>/dev/null)"
        if [ -n "$LEGACY_PID" ] && kill -0 "$LEGACY_PID" 2>/dev/null; then
            kill "$LEGACY_PID" 2>/dev/null || true
        fi
    fi
done

# 给旧业务进程一秒完成正常退出。
sleep 1

# 精确删除旧业务入口和被 SysV 误执行的旧传感器备份，保留正式硬件服务。
rm -f \
    /etc/init.d/S90autorun.pre-sc2356 \
    /etc/init.d/S93xingnian_wifi \
    /etc/init.d/S92bluetooth_audio \
    /etc/init.d/S93workcard_wifi \
    /etc/init.d/S94lite_p_mqtt \
    /etc/init.d/S95lite_p_stream

# 精确删除旧业务安装根目录及其中的旧凭据、日志状态和媒体库。
if [ -d "$LEGACY_ROOT" ] && [ "$LEGACY_ROOT" = "/opt/lite-p-stream" ]; then
    rm -rf -- "$LEGACY_ROOT"
fi

# 删除固定旧 PID 文件，避免后续安装误判。
rm -f \
    /run/lite-p-mqtt-agent.pid \
    /run/lite-p-device-stream.pid \
    /run/lite-p-stream-worker.pid

# 校验旧业务目录和自启动入口已经全部移除。
if [ -e "$LEGACY_ROOT" ] || \
   [ -e /etc/init.d/S90autorun.pre-sc2356 ] || \
   [ -e /etc/init.d/S93xingnian_wifi ] || \
   [ -e /etc/init.d/S92bluetooth_audio ] || \
   [ -e /etc/init.d/S93workcard_wifi ] || \
   [ -e /etc/init.d/S94lite_p_mqtt ] || \
   [ -e /etc/init.d/S95lite_p_stream ]; then
    echo "旧基线清理失败: 仍存在旧业务文件" >&2
    exit 70
fi

# 输出清理后的根分区空间供部署人员核对。
echo "旧 MQTT、媒体和旧路径 WiFi 基线已经清理"
df -h /
