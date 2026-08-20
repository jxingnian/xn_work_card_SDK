#ifndef WORKCARD_DESIRED_CONFIG_PARSER_H
#define WORKCARD_DESIRED_CONFIG_PARSER_H

// 引入基础整数和字符串长度类型。
#include <stddef.h>

// 定义后台下发的摄像头参数集合。
typedef struct {
    // 保存视频宽度。
    unsigned int width;
    // 保存视频高度。
    unsigned int height;
    // 保存视频帧率。
    unsigned int fps;
    // 保存关键帧间隔。
    unsigned int gop;
    // 保存视频码率。
    unsigned int bitrate_bps;
} workcard_desired_video_t;

// 定义一次 Desired 配置解析结果。
typedef struct {
    // 保存配置请求编号。
    char request_id[96];
    // 保存配置 revision。
    unsigned int revision;
    // 保存设备应答使用的 baseRevision。
    unsigned int base_revision;
    // 保存解析到的摄像头参数。
    workcard_desired_video_t video;
} workcard_desired_config_t;

// 解析并校验后台配置 JSON 中的摄像头参数。
int workcard_desired_config_parse(
    const char *payload,
    size_t payload_length,
    workcard_desired_config_t *config,
    char *error_message,
    size_t error_message_length);

#endif
