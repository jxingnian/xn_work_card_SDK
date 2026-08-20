# Work Card SDK 1.0.0

本目录是面向客户应用开发的正式 SDK 交付目录。客户通过稳定 C API 使用摄像头、音频、WiFi、经典蓝牙和以太网能力，不需要接触 Hi3516CV610、SC2356、G873/WS73U、RTL8723DS、海思 MPP 或 E-Hal 的底层实现源码。

## 1. 适用平台

第一版只支持以下固定组合：

| 项目 | 固定环境 |
| --- | --- |
| 核心板 | HongOu Lite_P / Hi3516CV610-20S |
| 摄像头 | SC2356 |
| WiFi | G873-USA1 / WS73U，仅启用 WiFi |
| 经典蓝牙 | 必联 BL-M8723DS1 / RTL8723DS，UART1/H5 |
| 目标 ABI | 32 位 ARM EABI5、soft-float、musl |
| 动态加载器 | `/lib/ld-musl-arm.so.1` |
| 系统环境 | 我方提供并验收的固定 rootfs/固件 |

本 SDK 不能直接部署到任意 ARM Linux、glibc rootfs 或其他核心板。新增硬件平台时保持公开 API，另行提供对应固件和服务端适配。

## 2. 已实现能力

- 设备、固件、SDK 和 ABI 版本查询。
- 模块能力查询。
- SC2356 H.264 视频输出、参数配置和 IDR 请求。
- 16 kHz 单声道 Opus 采集和下行播放。
- G873-USA1/WS73U WiFi 状态、扫描、WPA2 网络保存、连接、断开和删除。
- 必联 RTL8723DS 经典蓝牙状态、扫描、配对、连接、断开、删除配对和 A2DP Source。
- `demo/bin/arm-linux-musleabi/workcard-demo --bluetooth-demo [MAC]` 提供状态、扫描、配对连接和音频路由的公开 API 调用参考。
- `eth0` 和 `wlan0` 链路、IPv4、网关和 DNS 查询。
- GNSS 和 IMU 通用接口预留，当前固定固件明确返回 `WORKCARD_ERROR_NOT_SUPPORTED`。

第一版不提供 BLE、HFP/HSP、原始 YUV/NV12、JPEG 拍照、Air780EGP GNSS 和 IMU 采样。

固定固件禁止加载 WS73U 的 `ble_soc.ko`，也不启用必联 RTL8723DS 的 SDIO WiFi 功能。

## 3. 目录说明

| 目录 | 内容 |
| --- | --- |
| `include/workcard` | 客户唯一允许依赖的公开 C 头文件 |
| `lib/arm-linux-musleabi` | 客户应用链接的 `libworkcard_sdk.so` |
| `bin/arm-linux-musleabi` | 固定 rootfs 运行的底层硬件服务 |
| `runtime` | 目标板运行库、HAL 配置、OpenSSL 头文件和服务脚本 |
| `demo` | 使用当前 `iot.xingnian.vip` WSS 和 MQTT TLS 协议的工牌示例源码 |
| `toolchain` | 客户离线编译使用的固定 ARM musl 工具链和独立哈希 |
| `dist` | 可直接上传到小容量目标板的精简运行包及独立哈希 |
| `scripts` | 开发环境安装、Demo 编译、目标板安装、卸载和交付校验脚本 |
| `cmake` | ARM musl CMake 工具链文件 |
| `doc/API` | 逐模块 API 文档 |
| `doc/部署` | 环境、编译、安装、配置、升级和故障排查文档 |
| `licenses` | 第三方许可和商业授权边界说明 |

## 4. 最短使用流程

1. 在 Ubuntu 或 Windows WSL2 Ubuntu 中进入 SDK 根目录。
2. 执行 `./scripts/setup_dev_env.sh` 安装随附的固定工具链。
3. 执行 `./scripts/build_demo.sh` 验证客户编译环境。
4. 执行 `./scripts/create_target_bundle.sh` 生成目标板精简包。
5. 通过串口查询目标板当前 DHCP 地址并上传精简包到 `/run`。
6. 旧开发固件首次迁移时执行受保护的 `clean_legacy_target.sh`。
7. 在目标板执行 `scripts/install_target.sh` 安装 SDK 服务。
8. 客户程序引用 `#include <workcard/workcard_sdk.h>`。
9. 客户程序链接 `-lworkcard_sdk -lpthread`。
10. 调用 `workcard_sdk_create` 和 `workcard_sdk_start`。
11. 通过能力查询确认当前固件支持模块。
12. 调用摄像头、音频、网络和经典蓝牙 API。
13. 退出前按业务逆序停止摄像头并销毁 SDK。

编译和运行完整工牌示例见 `doc/工牌Demo说明.md`。

## 5. 关键约束

- 第一版底层服务只允许一个客户应用独占连接。
- 视频和音频回调运行在 SDK 接收线程，禁止在回调内长时间阻塞。
- 帧数据指针只在当前回调执行期间有效，异步使用前必须复制。
- 摄像头启动会初始化当前硬件统一视频、音频采集和播放链路。
- WiFi、蓝牙扫描是同步 API，客户应在业务工作线程调用。
- 云端连接和设备凭据不属于 SDK，由客户应用管理。
- Demo 中的 WSS、MQTT 和媒体协议源码只用于当前云端参考接入。
- Demo 已实现后台 MQTT Desired 摄像头配置、revision、回滚和状态持久化，详细流程见 `doc/部署/09_后台动态摄像头配置.md`。
- 所有文本文件和 API 字符串统一使用 UTF-8。
- Windows PowerShell 只用于启动 WSL，所有 `.sh`、`export`、`/opt` 和 `/mnt` 命令必须在 WSL Bash 中执行。

## 6. 安全要求

- 禁止将 `device_secret`、WiFi 密码和 TLS 私钥写入源码。
- 真实配置文件在目标板使用 `0600` 权限。
- SDK 和 Demo 日志不得输出密码和设备密钥。
- 发布前必须执行 `scripts/verify_package.sh` 并核对 `MANIFEST.sha256`。
- 厂商芯片运行库的对外再分发权必须由项目商务和法务确认。
