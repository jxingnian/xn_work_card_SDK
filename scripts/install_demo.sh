#!/bin/sh

# 任一 Demo 安装步骤失败时立即停止。
set -eu

# 解析当前脚本所在的 SDK 根目录。
SDK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# 要求客户先完成 Demo 交叉编译。
DEMO_BIN="$SDK_ROOT/demo/bin/arm-linux-musleabi/workcard-demo"
if [ ! -f "$DEMO_BIN" ]; then
    echo "Demo安装失败: 请先执行 scripts/build_demo.sh" >&2
    exit 66
fi

# 要求 TLS 服务器证书校验使用的固定 CA 包完整存在。
DEMO_CA_FILE="$SDK_ROOT/demo/config/ca-certificates.crt"
if [ ! -f "$DEMO_CA_FILE" ]; then
    echo "Demo安装失败: 缺少 $DEMO_CA_FILE" >&2
    exit 66
fi

# 要求交付包包含 Demo 的正式自启动服务。
DEMO_SERVICE="$SDK_ROOT/runtime/service/S94workcard_demo"
if [ ! -f "$DEMO_SERVICE" ]; then
    echo "Demo安装失败: 缺少 $DEMO_SERVICE" >&2
    exit 66
fi

# 要求蓝牙专用 Agent、工作器、测试音频和服务脚本完整存在。
for BLUETOOTH_FILE in \
    "$SDK_ROOT/bin/arm-linux-musleabi/workcard-bluetooth-agent" \
    "$SDK_ROOT/runtime/connectivity/bluetooth-scan-worker" \
    "$SDK_ROOT/runtime/connectivity/bluetooth-action-worker" \
    "$SDK_ROOT/runtime/service/S95workcard_bluetooth" \
    "$SDK_ROOT/demo/config/bluetooth-test.wav"; do
    if [ ! -f "$BLUETOOTH_FILE" ]; then
        echo "Demo安装失败: 缺少 $BLUETOOTH_FILE" >&2
        exit 66
    fi
done

# 要求在固定 rootfs 的 root 用户下执行安装。
if [ "$(id -u)" -ne 0 ]; then
    echo "Demo安装失败: 必须在目标板使用 root 用户执行" >&2
    exit 77
fi

# 创建只允许 root 写入的 Demo 目录。
install -d -m 0755 /opt/workcard-demo
install -d -m 0700 /opt/workcard-demo/log

# 安装 Demo 程序和不含真实凭据的配置模板。
install -m 0755 "$DEMO_BIN" /opt/workcard-demo/workcard-demo
install -m 0600 \
    "$SDK_ROOT/demo/config/workcard-demo.conf.example" \
    /opt/workcard-demo/workcard-demo.conf.example

# 安装只读 CA 包，保证固定 rootfs 不提供系统 CA 时仍能验证 WSS 证书链。
install -m 0644 "$DEMO_CA_FILE" /opt/workcard-demo/ca-certificates.crt

# 安装 Demo 自启动脚本，启动顺序固定在 SDK、蓝牙和 WiFi 之后。
install -m 0755 "$DEMO_SERVICE" /etc/init.d/S94workcard_demo

# 安装独立蓝牙 Agent、异步工作器和测试音频。
install -d -m 0700 /opt/workcard-bluetooth
install -m 0755 "$SDK_ROOT/bin/arm-linux-musleabi/workcard-bluetooth-agent" /opt/workcard-bluetooth/workcard-bluetooth-agent
install -m 0755 "$SDK_ROOT/runtime/connectivity/bluetooth-scan-worker" /opt/workcard-bluetooth/bluetooth-scan-worker
install -m 0755 "$SDK_ROOT/runtime/connectivity/bluetooth-action-worker" /opt/workcard-bluetooth/bluetooth-action-worker
install -m 0644 "$SDK_ROOT/demo/config/bluetooth-test.wav" /opt/workcard-bluetooth/bluetooth-test.wav
install -m 0755 "$SDK_ROOT/runtime/service/S95workcard_bluetooth" /etc/init.d/S95workcard_bluetooth

# 配置已经存在时立即启动 Demo，配置不存在时保留安装成功但不启动。
if [ -r /opt/workcard-demo/workcard-demo.conf ]; then
    /etc/init.d/S94workcard_demo start
    /etc/init.d/S95workcard_bluetooth start
else
    echo "Demo已安装但未启动: 缺少 /opt/workcard-demo/workcard-demo.conf"
fi

# 明确要求客户生成真实配置，避免示例安装时写入空凭据。
echo "Demo已安装，自启动服务为 /etc/init.d/S94workcard_demo"
echo "请基于 /opt/workcard-demo/workcard-demo.conf.example 创建权限为0600的 workcard-demo.conf"
