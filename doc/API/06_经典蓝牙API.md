# 经典蓝牙 API

第一版正式支持经典蓝牙扫描、配对、连接、断开、删除配对和 A2DP Source 音频输出；不支持 BLE GATT、HFP/HSP 和耳机麦克风。通话应用应使用设备麦克风采音，并根据 `workcard_audio_set_output_route` 选择 A2DP 播放或设备扬声器回退。

当前固定固件使用必联 BL-M8723DS1/RTL8723DS，通过 `/dev/ttyAMA1` 和 Realtek H5 注册 `hci0`。G873-USA1/WS73U 只负责 WiFi，其 `ble_soc.ko` 不进入正式镜像。硬件型号属于服务端实现细节，客户 API 不依赖这些名称。

设备 Demo 已提供后台动态配置：可按名称、MAC 或名称加 MAC 保存目标；MAC 缺失时设备扫描到唯一同名设备后自动学习并持久化 MAC；多个同名设备不会自动选择，必须管理员从扫描结果选择准确 MAC。

## 状态与电源

```c
workcard_result_t workcard_bluetooth_get_status(
    workcard_sdk_t *sdk,
    workcard_bluetooth_status_t *status);

workcard_result_t workcard_bluetooth_set_power(
    workcard_sdk_t *sdk,
    uint8_t enabled);
```

状态包含控制器地址、名称、上电状态、连接状态和 A2DP Source 服务状态。开关操作会管理固定固件中的驱动、D-Bus、BlueZ 和 BlueALSA 服务。

## 扫描

```c
workcard_result_t workcard_bluetooth_scan(
    workcard_sdk_t *sdk,
    uint32_t duration_seconds,
    workcard_bluetooth_device_t *devices,
    uint32_t capacity,
    uint32_t *device_count);
```

扫描时长范围为 1 到 30 秒，容量最大为 32。该函数同步等待扫描完成，必须在业务工作线程调用，SDK 请求超时应大于扫描时长。返回列表只包含远端 `Device`，不会把本机 `Controller` 地址或 `Discovering` 状态当作附近设备。

## 设备操作

```c
workcard_result_t workcard_bluetooth_pair_and_connect(workcard_sdk_t *sdk, const char *address);
workcard_result_t workcard_bluetooth_connect(workcard_sdk_t *sdk, const char *address);
workcard_result_t workcard_bluetooth_disconnect(workcard_sdk_t *sdk, const char *address);
workcard_result_t workcard_bluetooth_remove_pairing(workcard_sdk_t *sdk, const char *address);
```

地址必须使用 `XX:XX:XX:XX:XX:XX` 格式。配对并连接会依次执行配对、信任和连接。成功表示 BlueZ 命令完成；客户应再次查询设备状态确认 A2DP Profile 已连接。

## Demo 调用

在目标板上执行 `workcard-demo --bluetooth-demo` 可查询控制器并扫描十秒；执行 `workcard-demo --bluetooth-demo XX:XX:XX:XX:XX:XX` 会在扫描后调用配对、信任和连接 API，并设置蓝牙优先音频路由。该模式只验证 API，完成后退出，不启动云端媒体 Demo。

播放测试音频使用正式 BlueALSA 输出设备，示例文件是 `demo/config/bluetooth-test.wav`。公开 SDK 播放接口接收 16 kHz 单声道裸 Opus 包，WAV 文件不能直接作为 `workcard_audio_submit_playback` 参数；客户若需要 WAV 测试，应在应用层解码后按 Opus 包提交，或使用目标板 ALSA 的 `aplay` 播放。

## 已知设备

```c
workcard_result_t workcard_bluetooth_get_devices(
    workcard_sdk_t *sdk,
    workcard_bluetooth_device_t *devices,
    uint32_t capacity,
    uint32_t *device_count);
```

返回 BlueZ 当前已知设备的名称、地址、RSSI、配对、可信和连接状态。
