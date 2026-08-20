#ifndef XINGNIAN_MEDIA_PROTOCOL_H
#define XINGNIAN_MEDIA_PROTOCOL_H

// 定义视频媒体类型编号。
#define XN_MEDIA_TYPE_VIDEO 1

// 定义设备上传音频媒体类型编号。
#define XN_MEDIA_TYPE_AUDIO 2

// 定义后台下发对讲音频媒体类型编号。
#define XN_MEDIA_TYPE_INTERCOM_AUDIO 3

// 定义关键帧标记位。
#define XN_MEDIA_FLAG_KEY_FRAME 0x01

// 定义流不连续标记位，接收端必须清理旧解码缓冲。
#define XN_MEDIA_FLAG_DISCONTINUITY 0x02

#endif
