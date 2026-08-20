#ifndef WORKCARD_CAMERA_H
#define WORKCARD_CAMERA_H

// 引入公共类型、错误码和版本定义。
#include "workcard_types.h"

// 引入长度类型供视频帧描述使用。
#include <stddef.h>

// 允许公开头文件同时被 C 和 C++ 客户程序引用。
#ifdef __cplusplus
extern "C" {
#endif

// 前置声明 SDK 不透明句柄，客户不得访问内部字段。
typedef struct workcard_sdk workcard_sdk_t;

// 定义第一版支持的视频编码格式。
typedef enum workcard_video_codec {
    // 表示 H.264 Baseline 编码数据。
    WORKCARD_VIDEO_CODEC_H264 = 1
} workcard_video_codec_t;

// 定义视频帧携带的状态标志。
typedef enum workcard_video_frame_flag {
    // 表示当前 H.264 数据包含关键帧。
    WORKCARD_VIDEO_FRAME_KEY = 1U << 0,

    // 表示当前帧是一次新视频连续段的起点。
    WORKCARD_VIDEO_FRAME_DISCONTINUITY = 1U << 1
} workcard_video_frame_flag_t;

// 保存摄像头当前可配置范围。
typedef struct workcard_camera_capabilities {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 保存当前输出编码格式。
    workcard_video_codec_t codec;

    // 保存输出宽度下限。
    uint32_t minimum_width;

    // 保存输出宽度上限。
    uint32_t maximum_width;

    // 保存输出高度下限。
    uint32_t minimum_height;

    // 保存输出高度上限。
    uint32_t maximum_height;

    // 保存帧率下限。
    uint32_t minimum_fps;

    // 保存帧率上限。
    uint32_t maximum_fps;

    // 保存 H.264 GOP 下限。
    uint32_t minimum_gop;

    // 保存 H.264 GOP 上限。
    uint32_t maximum_gop;

    // 保存编码码率下限。
    uint32_t minimum_bitrate_bps;

    // 保存编码码率上限。
    uint32_t maximum_bitrate_bps;

    // 非零表示支持 H.264 关键帧请求。
    uint8_t supports_keyframe_request;

    // 非零表示支持原始 NV12/YUV 帧，本版固定为零。
    uint8_t supports_raw_frame;

    // 非零表示支持 JPEG 拍照，本版固定为零。
    uint8_t supports_jpeg_snapshot;

    // 保存用于未来扩展的对齐空间，调用方必须清零。
    uint8_t reserved;
} workcard_camera_capabilities_t;

// 保存一次摄像头启动使用的完整编码配置。
typedef struct workcard_camera_config {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 保存期望输出宽度，当前要求为偶数。
    uint32_t width;

    // 保存期望输出高度，当前要求为偶数。
    uint32_t height;

    // 保存期望帧率。
    uint32_t fps;

    // 保存期望 H.264 GOP。
    uint32_t gop;

    // 保存期望 H.264 码率。
    uint32_t bitrate_bps;
} workcard_camera_config_t;

// 保存 SDK 回调给客户的一帧 H.264 数据。
typedef struct workcard_video_frame {
    // SDK 填写当前结构体大小。
    uint32_t struct_size;

    // 保存当前视频编码格式。
    workcard_video_codec_t codec;

    // 保存关键帧和不连续等位标志。
    uint32_t flags;

    // 保存从零递增的视频帧序号。
    uint32_t sequence;

    // 保存设备单调时钟微秒时间戳。
    uint64_t timestamp_us;

    // 指向当前 H.264 数据，指针只在回调执行期间有效。
    const uint8_t *data;

    // 保存当前数据字节数。
    size_t data_size;
} workcard_video_frame_t;

// 定义视频帧回调，客户应快速复制或处理数据并立即返回。
typedef void (*workcard_video_frame_callback_t)(const workcard_video_frame_t *frame, void *user_data);

// 查询当前摄像头正式开放的参数范围和能力。
workcard_result_t workcard_camera_get_capabilities(
    workcard_sdk_t *sdk,
    workcard_camera_capabilities_t *capabilities);

// 使用完整配置启动摄像头和 H.264 输出。
workcard_result_t workcard_camera_start(
    workcard_sdk_t *sdk,
    const workcard_camera_config_t *config,
    workcard_video_frame_callback_t callback,
    void *user_data);

// 请求编码器尽快输出一个 H.264 IDR 关键帧。
workcard_result_t workcard_camera_request_keyframe(workcard_sdk_t *sdk);

// 停止摄像头并释放当前客户占用的媒体资源。
workcard_result_t workcard_camera_stop(workcard_sdk_t *sdk);

// 结束 C ABI 声明区域。
#ifdef __cplusplus
}
#endif

#endif
