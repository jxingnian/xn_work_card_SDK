#ifndef WORKCARD_AUDIO_H
#define WORKCARD_AUDIO_H

// 引入公共类型和 SDK 句柄使用的基础定义。
#include "workcard_types.h"

// 引入长度类型供音频数据描述使用。
#include <stddef.h>

// 允许公开头文件同时被 C 和 C++ 客户程序引用。
#ifdef __cplusplus
extern "C" {
#endif

// 前置声明 SDK 不透明句柄。
typedef struct workcard_sdk workcard_sdk_t;

// 定义第一版公开的音频编码格式。
typedef enum workcard_audio_codec {
    // 表示 16 kHz 单声道裸 Opus 数据。
    WORKCARD_AUDIO_CODEC_OPUS = 1,

    // 表示有符号十六位小端 PCM 数据，具体后端支持情况通过能力查询确定。
    WORKCARD_AUDIO_CODEC_PCM_S16LE = 2
} workcard_audio_codec_t;

// 定义通话下行音频的正式输出策略。
typedef enum workcard_audio_output_route {
    // 蓝牙已连接时优先使用 A2DP，蓝牙不可用时自动回退设备扬声器。
    WORKCARD_AUDIO_OUTPUT_BLUETOOTH_PREFERRED = 1,

    // 即使蓝牙保持连接也强制使用设备自带扬声器。
    WORKCARD_AUDIO_OUTPUT_SPEAKER = 2
} workcard_audio_output_route_t;

// 保存音频模块能力。
typedef struct workcard_audio_capabilities {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 保存当前正式采集编码格式。
    workcard_audio_codec_t capture_codec;

    // 保存当前正式播放编码格式。
    workcard_audio_codec_t playback_codec;

    // 保存固定采样率。
    uint32_t sample_rate;

    // 保存固定声道数。
    uint32_t channels;

    // 非零表示支持采集回调。
    uint8_t supports_capture;

    // 非零表示支持下行播放。
    uint8_t supports_playback;

    // 非零表示当前固件支持蓝牙 A2DP 输出路由。
    uint8_t supports_bluetooth_output;

    // 保存用于未来扩展的对齐空间。
    uint8_t reserved;
} workcard_audio_capabilities_t;

// 保存 SDK 回调给客户的一包音频数据。
typedef struct workcard_audio_frame {
    // SDK 填写当前结构体大小。
    uint32_t struct_size;

    // 保存当前音频编码格式。
    workcard_audio_codec_t codec;

    // 保存不连续等位标志。
    uint32_t flags;

    // 保存从零递增的音频包序号。
    uint32_t sequence;

    // 保存设备单调时钟微秒时间戳。
    uint64_t timestamp_us;

    // 指向当前音频数据，指针只在回调执行期间有效。
    const uint8_t *data;

    // 保存当前数据字节数。
    size_t data_size;
} workcard_audio_frame_t;

// 定义音频采集回调，客户应快速复制或处理数据并立即返回。
typedef void (*workcard_audio_frame_callback_t)(const workcard_audio_frame_t *frame, void *user_data);

// 查询音频采集、播放和蓝牙输出能力。
workcard_result_t workcard_audio_get_capabilities(
    workcard_sdk_t *sdk,
    workcard_audio_capabilities_t *capabilities);

// 注册音频采集回调，当前硬件在摄像头媒体链路启动后开始产生 Opus 数据。
workcard_result_t workcard_audio_set_capture_callback(
    workcard_sdk_t *sdk,
    workcard_audio_frame_callback_t callback,
    void *user_data);

// 开启或关闭下行 Opus 播放接收状态。
workcard_result_t workcard_audio_set_playback_enabled(workcard_sdk_t *sdk, uint8_t enabled);

// 设置通话下行音频输出策略，配置在当前 SDK 服务进程生命周期内生效。
workcard_result_t workcard_audio_set_output_route(
    workcard_sdk_t *sdk,
    workcard_audio_output_route_t route);

// 向内置扬声器或已连接的蓝牙 A2DP 设备提交一包裸 Opus 数据。
workcard_result_t workcard_audio_submit_playback(
    workcard_sdk_t *sdk,
    const workcard_audio_frame_t *frame);

// 结束 C ABI 声明区域。
#ifdef __cplusplus
}
#endif

#endif
