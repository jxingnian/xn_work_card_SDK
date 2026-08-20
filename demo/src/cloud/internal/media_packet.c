#include "media_packet.h"

#include <stdlib.h>
#include <string.h>

// 按网络字节序读取四字节整数。
static uint32_t xn_read_u32_be(const uint8_t *source)
{
    // 合并四个字节得到无符号整数。
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           (uint32_t)source[3];
}

// 按网络字节序读取八字节整数。
static uint64_t xn_read_u64_be(const uint8_t *source)
{
    // 初始化读取结果。
    uint64_t value = 0;

    // 逐字节合并大端序时间戳。
    for (unsigned int byte_index = 0; byte_index < 8; byte_index++) {
        value = (value << 8) | source[byte_index];
    }

    // 返回读取后的无符号时间戳。
    return value;
}

// 按网络字节序写入四字节整数。
static void xn_write_u32_be(uint8_t *target, uint32_t value)
{
    // 写入最高有效字节。
    target[0] = (uint8_t)(value >> 24);

    // 写入次高有效字节。
    target[1] = (uint8_t)(value >> 16);

    // 写入次低有效字节。
    target[2] = (uint8_t)(value >> 8);

    // 写入最低有效字节。
    target[3] = (uint8_t)value;
}

// 按网络字节序写入八字节整数。
static void xn_write_u64_be(uint8_t *target, uint64_t value)
{
    // 逐字节写入大端序时间戳。
    for (unsigned int byte_index = 0; byte_index < 8; byte_index++) {
        target[byte_index] = (uint8_t)(value >> ((7U - byte_index) * 8U));
    }
}

// 构建符合星年设备协议的完整二进制媒体包。
int xn_media_packet_build(
    uint8_t media_type,
    uint8_t flags,
    uint32_t sequence,
    uint64_t timestamp_us,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t **packet,
    size_t *packet_length)
{
    // 计算完整媒体包长度。
    size_t total_length = XN_MEDIA_HEADER_LENGTH + payload_length;

    // 为完整媒体包分配连续内存。
    uint8_t *packet_buffer = malloc(total_length);

    // 内存不足时返回失败。
    if (packet_buffer == NULL) {
        return -1;
    }

    // 写入固定魔数 XNMS。
    memcpy(packet_buffer, "XNMS", 4);

    // 写入当前媒体协议版本。
    packet_buffer[4] = 1;

    // 写入媒体类型。
    packet_buffer[5] = media_type;

    // 写入媒体标志位。
    packet_buffer[6] = flags;

    // 写入保留字段。
    packet_buffer[7] = 0;

    // 写入媒体包递增序号。
    xn_write_u32_be(packet_buffer + 8, sequence);

    // 写入统一微秒时间戳。
    xn_write_u64_be(packet_buffer + 12, timestamp_us);

    // 写入媒体负载长度。
    xn_write_u32_be(packet_buffer + 20, (uint32_t)payload_length);

    // 将媒体负载复制到固定头部之后。
    memcpy(packet_buffer + XN_MEDIA_HEADER_LENGTH, payload, payload_length);

    // 返回构建后的媒体包地址。
    *packet = packet_buffer;

    // 返回构建后的媒体包长度。
    *packet_length = total_length;

    // 返回构建成功。
    return 0;
}

// 校验并解析服务器下发的完整星年媒体协议包。
int xn_media_packet_parse(
    const uint8_t *packet,
    size_t packet_length,
    xn_media_packet_view *packet_view)
{
    // 非法输入参数直接返回失败。
    if (packet == NULL || packet_view == NULL) {
        return -1;
    }

    // 完整协议包必须至少包含固定头部。
    if (packet_length < XN_MEDIA_HEADER_LENGTH) {
        return -1;
    }

    // 校验固定协议魔数 XNMS。
    if (memcmp(packet, "XNMS", 4) != 0) {
        return -1;
    }

    // 当前设备只接受第一版媒体协议。
    if (packet[4] != 1) {
        return -1;
    }

    // 读取协议声明的媒体负载长度。
    uint32_t payload_length = xn_read_u32_be(packet + 20);

    // 声明负载长度必须与实际包长严格一致。
    if ((size_t)payload_length != packet_length - XN_MEDIA_HEADER_LENGTH) {
        return -1;
    }

    // 填充解析后的媒体类型。
    packet_view->media_type = packet[5];

    // 填充解析后的媒体标志位。
    packet_view->flags = packet[6];

    // 填充解析后的媒体包序号。
    packet_view->sequence = xn_read_u32_be(packet + 8);

    // 填充解析后的媒体包时间戳。
    packet_view->timestamp_us = xn_read_u64_be(packet + 12);

    // 填充解析后的媒体负载地址。
    packet_view->payload = packet + XN_MEDIA_HEADER_LENGTH;

    // 填充解析后的媒体负载长度。
    packet_view->payload_length = payload_length;

    // 返回媒体协议解析成功。
    return 0;
}
