# 音频 API

## 能力查询

```c
workcard_result_t workcard_audio_get_capabilities(
    workcard_sdk_t *sdk,
    workcard_audio_capabilities_t *capabilities);
```

当前固定为 16 kHz、单声道、裸 Opus 采集和播放，支持内置扬声器及蓝牙 A2DP Source 输出。

## 通话输出策略

```c
typedef enum workcard_audio_output_route {
    WORKCARD_AUDIO_OUTPUT_BLUETOOTH_PREFERRED = 1,
    WORKCARD_AUDIO_OUTPUT_SPEAKER = 2
} workcard_audio_output_route_t;

workcard_result_t workcard_audio_set_output_route(
    workcard_sdk_t *sdk,
    workcard_audio_output_route_t route);
```

`WORKCARD_AUDIO_OUTPUT_BLUETOOTH_PREFERRED` 表示通话开始后优先使用已连接的 A2DP 耳机，蓝牙不可用时自动回退设备扬声器；`WORKCARD_AUDIO_OUTPUT_SPEAKER` 表示即使蓝牙保持连接也强制使用设备扬声器。两种模式的采音都来自设备自带麦克风。

## 注册采集回调

```c
workcard_result_t workcard_audio_set_capture_callback(
    workcard_sdk_t *sdk,
    workcard_audio_frame_callback_t callback,
    void *user_data);
```

允许传空回调取消接收。当前硬件在 `workcard_camera_start` 成功后产生 Opus 数据。

回调中的 `data` 只在当前调用期间有效。`timestamp_us` 与视频使用同一设备单调时钟，可用于音视频同步。

## 播放门控

```c
workcard_result_t workcard_audio_set_playback_enabled(
    workcard_sdk_t *sdk,
    uint8_t enabled);
```

播放前必须设置为 1。设置为 0 会关闭接收并清理旧播放缓冲。媒体运行时未启动时返回 `WORKCARD_ERROR_NOT_RUNNING`。

## 提交下行 Opus

```c
workcard_result_t workcard_audio_submit_playback(
    workcard_sdk_t *sdk,
    const workcard_audio_frame_t *frame);
```

只接受 `WORKCARD_AUDIO_CODEC_OPUS`。蓝牙 A2DP 实际可用时优先输出到耳机，否则使用内置 AO。客户必须按编码包边界提交，不能拆分一个 Opus 包。
