#ifndef XINGNIAN_CONFIG_H
#define XINGNIAN_CONFIG_H

#include <stddef.h>

// 定义文本配置字段的最大长度。
#define XN_CONFIG_TEXT_LENGTH 256

// 保存设备推流程序的全部运行参数。
typedef struct {
    // 保存设备连接的加密 WebSocket 地址。
    char server_url[XN_CONFIG_TEXT_LENGTH];

    // 保存服务器证书校验使用的 CA 文件路径。
    char ca_file[XN_CONFIG_TEXT_LENGTH];

    // 保存后台分配的设备编号。
    char device_id[XN_CONFIG_TEXT_LENGTH];

    // 保存后台仅展示一次的设备密钥。
    char device_secret[XN_CONFIG_TEXT_LENGTH];

    // 保存 MQTT TLS Broker 主机名。
    char mqtt_host[XN_CONFIG_TEXT_LENGTH];

    // 保存 MQTT TLS Broker 端口。
    unsigned int mqtt_port;

    // 保存后台为当前设备分配的 MQTT 独立用户名。
    char mqtt_username[XN_CONFIG_TEXT_LENGTH];

    // 保存后台为当前设备分配的 MQTT 独立密码。
    char mqtt_password[XN_CONFIG_TEXT_LENGTH];

    // 保存设备端程序版本号。
    char firmware_version[64];

    // 保存兼容历史配置字段的占位状态文件路径。
    char managed_state_file[XN_CONFIG_TEXT_LENGTH];

    // 保存动态摄像头配置已经确认生效的 revision。
    unsigned int managed_revision;

    // 保存视频输出宽度。
    unsigned int video_width;

    // 保存视频输出高度。
    unsigned int video_height;

    // 保存视频输出帧率。
    unsigned int video_fps;

    // 保存视频关键帧间隔。
    unsigned int video_gop;

    // 保存 H.264 视频目标码率。
    unsigned int video_bitrate_bps;

    // 保存音频采样率。
    unsigned int audio_sample_rate;

    // 保存音频声道数。
    unsigned int audio_channels;

    // 保存有线备用网络接口名称。
    char wired_interface[XN_CONFIG_TEXT_LENGTH];

    // 保存有线备用网络网关地址。
    char wired_gateway[XN_CONFIG_TEXT_LENGTH];

    // 保存网络切换和 WiFi 关联的最大等待秒数。
    unsigned int network_switch_timeout_seconds;

    // 保存 Demo 已同步 WiFi 配置清单的本地状态文件路径。
    char wifi_state_file[XN_CONFIG_TEXT_LENGTH];
} xn_app_config;

// 写入稳定且低带宽的默认配置。
void xn_config_set_defaults(xn_app_config *config);

// 从 UTF-8 键值配置文件读取运行参数。
int xn_config_load(const char *file_path, xn_app_config *config, char *error_message, size_t error_message_length);

#endif
