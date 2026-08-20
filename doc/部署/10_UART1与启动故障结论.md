# UART1 与启动故障结论

## 1. UART1 已确认事实

厂家必联驱动规定 RTL8723D 使用 Realtek H5，初始参数是 115200 8E1。板端只读寄存器和驱动统计得到：

```text
UART1 parent clock: 100000000 Hz
UART1 divisor baud: 115207
核心板 35 RX mux: 7
核心板 37 TX mux: 7
PL011 RIS: 0x180
/proc/tty/driver/ttyAMA: fe=160 pe=229
```

`RIS 0x80` 是帧错误，`RIS 0x100` 是奇偶校验错误。时钟、波特率分频、RX/TX pinmux 和 UART 时钟门均正确，因此软件没有把 UART1 配成错误端口或错误时钟。

原始 H5 同步帧对照结果：

```text
115200-8E1 received=13 hex=00000000000000140000000000
115200-8N1 received=12 hex=4DFF4D004DFF4DFE4DFF0000
115200-8O1 received=12 hex=00FF00FF001600FF00FF0000
```

三组都不是 H5 同步响应，且 8E1 仍产生错误字节。确定结论是：当前到达主控 UART1_RXD 的电气波形不构成有效的 115200 8E1 H5 帧；后台扫描、BlueZ 和 SDK 解析代码尚未获得 `hci0`，它们不是本次扫描为空的原因。

## 2. 已落地的软件修复

正式 `sys_config.ko` 已把四个引脚统一设置为功能 7：

```text
核心板 33 -> UART1_CTSN
核心板 34 -> UART1_RTSN
核心板 35 -> UART1_RXD
核心板 37 -> UART1_TXD
```

代码只修改 IOCFG 低四位功能号，保留厂家配置的上下拉和驱动强度。新模块 SHA-256：

```text
0a64226a63ea72b9210a59d2a51ed805396fb69dd3f88ebcf2c71650ad050eed
```

## 3. 必须完成的正式接线

```text
核心板 35 UART1_RXD  <- 模组 P42 UART_TX
核心板 37 UART1_TXD  -> 模组 P43 UART_RX
核心板 34 UART1_RTSN -> 模组 P44 UART_CTS
核心板 33 UART1_CTSN -> GND
模组 UART_RTS       -> NC
模组 P34 BT_DIS#    -> 3.3V
```

P44 直接接地只表示控制器始终允许发送，不是厂家量产接线。核心板33脚已经专用于蓝牙CTS，按键必须迁移到其他GPIO。模组完成真正冷启动后执行 `scripts/verify_target_bluetooth.sh`；只有 `hci0`、`rtk_hciattach`、`bluetoothd` 和 `bluealsad` 同时正常才算底层蓝牙验收完成。

## 4. U-Boot 无响应是独立故障

2026-08-20 软件重启日志明确出现：

```text
Loading Environment from NAND... bad CRC
## Error: "bootcmd" not defined
```

因此本次重启后停在 U-Boot `#` 的直接原因是 NAND 环境校验失败，不是 Linux 内核、蓝牙或媒体服务卡死。恢复 `bootargs`、`bootcmd` 和 `saveenv` 的正式命令见《07_故障排查》。未恢复 `bootargs` 时手工执行 `bootm` 会停在 `Starting kernel`，因为内核没有得到 UBI rootfs 参数。
