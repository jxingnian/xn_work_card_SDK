# 工牌 Demo 说明

## 1. 功能

Demo 展示客户应用如何：

1. 加载外部设备凭据和媒体参数。
2. 创建并连接底层 SDK。
3. 查询设备版本和九个模块能力。
4. 注册 Opus 采集回调。
5. 启动 SC2356 H.264 输出。
6. 使用当前 `wss://iot.xingnian.vip/device-stream` 完成鉴权。
7. 响应 `media-start` 和 `media-stop` 上传音视频。
8. 响应 `intercom-start` 和 `intercom-stop` 播放下行 Opus。
9. 每十五秒发送应用层心跳。
10. 断线后执行有上限的指数退避重连。
11. 收到 `SIGINT` 或 `SIGTERM` 后按顺序释放资源。
12. 通过 MQTT TLS 发布设备在线状态、能力和 reported 状态，断线后自动退避重连。
13. 展示当前 WiFi 状态、附近网络扫描、配置清单同步和独立连接动作。
14. WiFi 连接前验证有线云端通路，连接成功切回 WiFi，失败保留有线管理通道。

## 2. 源码模块

| 文件 | 职责 |
| --- | --- |
| `demo/src/main.c` | SDK 生命周期、媒体回调和安全退出 |
| `demo/src/cloud/cloud_demo.c` | 当前云端鉴权、控制、心跳和重连 |
| `demo/src/cloud/internal/websocket_client.c` | TLS 1.2、证书校验和 RFC 6455 客户端 |
| `demo/src/cloud/internal/media_packet.c` | 当前 24 字节媒体协议封包与解析 |
| `demo/src/cloud/internal/config.c` | UTF-8 键值配置读取和严格校验 |
| `demo/src/mqtt/mqtt_demo.c` | MQTT 3.1.1 TLS、Presence、能力和状态上报示例 |
| `demo/src/mqtt/internal/mqtt_tls_platform.c` | Paho Embedded C 的 OpenSSL TLS 传输适配 |
| `demo/src/mqtt/internal/wifi_command_parser.c` | WiFi MQTT 命令的结构化解析和边界校验 |
| `demo/src/network/network_failover.c` | 有线云端探测、临时路由切换和 WiFi 路由恢复 |
| `demo/src/network/wifi_profile_store.c` | 不含密码的 WiFi 配置清单原子持久化 |

## 3. 凭据

复制 `demo/config/workcard-demo.conf.example` 的字段内容生成客户自己的配置。真实配置不得提交到源码仓库，目标板权限必须为 `0600`。

Demo 不打印 `device_secret` 或 `mqtt_password`。WSS 和 MQTT 使用两套独立凭据，生产项目应由客户自己的安全配置模块提供凭据，不能把密钥固化在二进制常量中。

## 4. 编译

必须在 Ubuntu 或 Windows WSL2 Ubuntu 的 Bash 中执行。首次使用先安装 SDK 随附工具链：

```bash
./scripts/setup_dev_env.sh
./scripts/build_demo.sh
```

编译脚本自动发现固定工具链，不需要手工设置路径。输出为 `demo/build/bin/workcard-demo`，并同步更新 `demo/bin/arm-linux-musleabi/workcard-demo`。

## 5. 运行

```sh
export LD_LIBRARY_PATH=/opt/workcard-sdk/lib:/lib:/usr/lib
/opt/workcard-demo/workcard-demo --config /opt/workcard-demo/workcard-demo.conf
```

运行前必须确认：

- `S91workcard_sdk status` 返回 running。
- 设备时间已经同步，否则 TLS 证书校验会失败。
- CA 文件存在且可读。
- `device_id` 和 `device_secret` 与当前后台设备一致。
- `mqtt_username` 和 `mqtt_password` 是该设备独立签发的 MQTT 凭据。
- 网络可以解析并访问 `iot.xingnian.vip`。
- `wired_interface` 已取得 IPv4，且 `wired_gateway` 可以访问 MQTT Broker 端口。

`install_demo.sh` 会同时安装 SDK 随附的公开 CA 包。Demo 将标准输出设置为行缓冲、标准错误设置为无缓冲，后台重定向日志时也能及时看到连接和鉴权状态。

启动后日志应同时出现：

```text
云端鉴权成功，等待实时查看指令
MQTT连接成功，设备在线状态已发布
```

后台设备列表的在线状态由 MQTT retained Presence 维护；仅建立 WSS 连接不会使设备列表变为在线。

## 6. WiFi 后台配置流程

1. 后台读取 `state/reported.network.wifi` 显示当前 SSID、BSSID、IPv4 和 RSSI。
2. 管理员点击扫描，扫描结果只允许添加到配置，不直接连接。
3. 管理员填写密码并保存，Demo 只同步配置清单，不切换当前网络。
4. 管理员点击已保存配置的连接按钮后，Demo 才执行有线保活和 WiFi 切换。
5. WiFi 成功取得 IPv4 后恢复 WiFi 默认路由；失败时保留有线默认路由供后台继续修复。

路由切换时原 MQTT TCP 会话会短暂重连。Demo 保存未确认的动作结果，并在新会话建立后优先补发。`wifi-state.conf` 只保存 profileId、SSID、优先级、启用状态和 revision，权限为 `0600`，不保存密码或 PSK。

## 7. 参考代码边界

Demo 云端源码会向客户公开当前鉴权、媒体和 MQTT 协议，用于客户应用参考。客户可替换整个 `demo/src/cloud` 和 `demo/src/mqtt`，底层 SDK 不依赖这些目录。
