#ifndef XINGNIAN_MEDIA_PACKET_H
#define XINGNIAN_MEDIA_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "media_protocol.h"

// 定义媒体协议固定头部长度。
#define XN_MEDIA_HEADER_LENGTH 24

// 保存解析后的星年媒体协议只读视图。
typedef struct {
    // 保存媒体类型编号。
    uint8_t media_type;

    // 保存媒体标志位。
    uint8_t flags;

    // 保存媒体包递增序号。
    uint32_t sequence;

    // 保存媒体包微秒时间戳。
    uint64_t timestamp_us;

    // 保存媒体负载地址。
    const uint8_t *payload;

    // 保存媒体负载长度。
    size_t payload_length;
} xn_media_packet_view;

// 构建符合星年设备协议的完整二进制媒体包。
int xn_media_packet_build(
    uint8_t media_type,
    uint8_t flags,
    uint32_t sequence,
    uint64_t timestamp_us,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t **packet,
    size_t *packet_length);

// 校验并解析服务器下发的完整星年媒体协议包。
int xn_media_packet_parse(
    const uint8_t *packet,
    size_t packet_length,
    xn_media_packet_view *packet_view);

#endif
