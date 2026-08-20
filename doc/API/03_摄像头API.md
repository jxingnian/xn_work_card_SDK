# 摄像头 API

## 能力查询

```c
workcard_result_t workcard_camera_get_capabilities(
    workcard_sdk_t *sdk,
    workcard_camera_capabilities_t *capabilities);
```

当前 SC2356 能力：

- H.264 Baseline。
- 宽度 160 到 1600，要求偶数。
- 高度 120 到 1200，要求偶数。
- 帧率 1 到 30 fps。
- GOP 1 到 300。
- 码率 64000 到 4000000 bps。
- 支持请求 IDR。
- 不支持公开 YUV/NV12 和 JPEG 拍照。

## 启动

```c
workcard_result_t workcard_camera_start(
    workcard_sdk_t *sdk,
    const workcard_camera_config_t *config,
    workcard_video_frame_callback_t callback,
    void *user_data);
```

`config` 必须一次性提供宽、高、帧率、GOP 和码率。参数不会在运行中直接修改；需要修改时先停止，再使用完整新配置启动。

当前硬件使用统一媒体运行时，因此启动摄像头时同时初始化 Opus 采集和下行播放链路。建议先调用 `workcard_audio_set_capture_callback`，再启动摄像头。

## 视频回调

```c
void callback(const workcard_video_frame_t *frame, void *user_data);
```

- `codec` 当前为 `WORKCARD_VIDEO_CODEC_H264`。
- `flags` 可包含 `WORKCARD_VIDEO_FRAME_KEY` 和 `WORKCARD_VIDEO_FRAME_DISCONTINUITY`。
- `sequence` 为当前媒体会话内递增序号。
- `timestamp_us` 为设备单调时钟微秒值，不能当作 UTC 时间。
- `data` 为 H.264 Annex-B 数据，只在回调期间有效。

客户应把耗时网络发送放入自己的有界队列，不能长期阻塞 SDK 接收线程。

## 请求关键帧

```c
workcard_result_t workcard_camera_request_keyframe(workcard_sdk_t *sdk);
```

摄像头未运行时返回 `WORKCARD_ERROR_NOT_RUNNING`。请求成功表示命令已经提交给编码器，不保证调用返回时 IDR 已经到达。

## 停止

```c
workcard_result_t workcard_camera_stop(workcard_sdk_t *sdk);
```

停止统一媒体运行时并释放摄像头、编码器、麦克风和播放链路。已经停止时返回 `WORKCARD_ERROR_NOT_RUNNING`。
