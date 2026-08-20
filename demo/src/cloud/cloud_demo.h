#ifndef WORKCARD_CLOUD_DEMO_H
#define WORKCARD_CLOUD_DEMO_H

// 引入底层 SDK 和 Demo 配置定义。
#include <workcard/workcard_sdk.h>
#include "internal/config.h"

// 隐藏云端参考实现的线程和连接状态。
typedef struct workcard_cloud_demo workcard_cloud_demo_t;

// 创建使用当前 iot.xingnian.vip 协议的云端参考客户端。
int workcard_cloud_demo_create(
    workcard_sdk_t *sdk,
    const xn_app_config *config,
    workcard_cloud_demo_t **cloud);

// 启动 WSS 连接、鉴权、心跳和重连线程。
int workcard_cloud_demo_start(workcard_cloud_demo_t *cloud);

// 将 SDK 回调的 H.264 帧按当前媒体协议发送到云端。
void workcard_cloud_demo_submit_video(
    workcard_cloud_demo_t *cloud,
    const workcard_video_frame_t *frame);

// 将 SDK 回调的 Opus 包按当前媒体协议发送到云端。
void workcard_cloud_demo_submit_audio(
    workcard_cloud_demo_t *cloud,
    const workcard_audio_frame_t *frame);

// 更新下一次 WSS 鉴权使用的动态视频参数。
void workcard_cloud_demo_update_video_config(
    workcard_cloud_demo_t *cloud,
    const workcard_camera_config_t *config);

// 停止网络线程并关闭当前 WSS 连接。
void workcard_cloud_demo_stop(workcard_cloud_demo_t *cloud);

// 销毁云端参考客户端私有状态。
void workcard_cloud_demo_destroy(workcard_cloud_demo_t *cloud);

#endif
