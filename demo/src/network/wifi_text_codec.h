#ifndef WORKCARD_WIFI_TEXT_CODEC_H
#define WORKCARD_WIFI_TEXT_CODEC_H

// 引入固定字符串容量类型。
#include <stddef.h>

// 将 WiFi 扫描服务返回的连续 \xHH 转义还原为原始 UTF-8 字节。
int workcard_wifi_decode_scan_text(const char *source, char *target, size_t capacity);

#endif
