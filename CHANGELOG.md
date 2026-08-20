# 变更记录

## 1.0.0 - 2026-08-17

- 建立稳定的 `workcard_*` C ABI。
- 增加客户端动态库和独立底层硬件服务。
- 接入 SC2356 H.264、Opus 采集与播放。
- 接入 G873-USA1/WS73U WiFi 和必联 BL-M8723DS1/RTL8723DS UART 经典蓝牙控制。
- 禁用 WS73U BLE/SLE 和 RTL8723DS SDIO WiFi，避免两个组合模组职责混用。
- 增加以太网与 WiFi 网络状态查询。
- 增加 GNSS 和 IMU 能力预留，当前固件明确报告不支持。
- 增加当前 `iot.xingnian.vip` WSS 工牌 Demo。
- Demo 增加 MQTT TLS、设备在线 Presence、能力和 reported 状态上报。
- Demo 增加后台 Desired 摄像头配置接收、参数校验、媒体重建、回滚和原子状态持久化。
- SDK 服务增加退出状态记录和媒体 HAL 异常冷启动保护，Demo 在普通连接断开时自动重建会话。
- 增加离线交叉编译、目标板安装、卸载和交付校验脚本。
- 随 SDK 提供固定 ARM musl 工具链归档、独立 SHA-256 和免 sudo 安装脚本。
- Demo 与 CMake 默认自动发现当前 Linux 用户安装的固定工具链。
- 增加小容量目标板精简包、独立 SHA-256 和受保护的旧业务基线清理流程。
- WiFi 状态和网络基础组件完全移出旧 `/opt/lite-p-stream` 路径。
- Demo 安装增加固定 CA 包并保证后台日志及时刷新。
- 修复 WSL 在 Windows NTFS 上重复构建时的权限兼容问题。
- 修复固定 rootfs 中 D-Bus 厂商构建路径导致经典蓝牙命令崩溃的问题。
- Demo 增加 WiFi 状态、附近网络扫描、配置与连接分离、MQTT 动态配置和动作结果补发。
- Demo 增加有线 MQTT 端口预检、WiFi 配置期间路由保活、成功切回 WiFi 和失败保留有线策略。
- 增加蓝牙、WiFi、重启和旧业务清理的目标板验收步骤。
- 清理被 SysV 误执行的旧 `fpx1002` 传感器自启动备份。
- 增加 `S94workcard_demo` 正式自启动服务，持久化 Demo 业务日志和退出状态，修复开发板重启后后台离线问题。
