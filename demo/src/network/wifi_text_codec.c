// 引入 WiFi 扫描文本转换接口。
#include "wifi_text_codec.h"

// 引入固定宽度整数和字符串接口。
#include <stdint.h>
#include <string.h>

// 将一个十六进制字符转换为数值。
static int workcard_wifi_hex_value(char character)
{
    // 处理数字字符。
    if (character >= '0' && character <= '9') return character - '0';
    // 处理小写十六进制字符。
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    // 处理大写十六进制字符。
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    // 其他字符不是十六进制值。
    return -1;
}

// 解码扫描服务使用的十六进制转义文本。
int workcard_wifi_decode_scan_text(const char *source, char *target, size_t capacity)
{
    // 校验输入和输出缓冲区。
    if (source == NULL || target == NULL || capacity == 0U) return -1;
    // 逐字节复制普通字符并还原 \xHH 序列。
    size_t source_index = 0U;
    size_t target_index = 0U;
    while (source[source_index] != '\0') {
        unsigned int output_byte = (unsigned char)source[source_index++];
        if (output_byte == '\\' && source[source_index] == 'x' && source[source_index + 1U] != '\0' && source[source_index + 2U] != '\0') {
            const int high = workcard_wifi_hex_value(source[source_index + 1U]);
            const int low = workcard_wifi_hex_value(source[source_index + 2U]);
            if (high >= 0 && low >= 0) {
                output_byte = (unsigned int)((high << 4) | low);
                source_index += 3U;
            }
        }
        // 每个还原字节都必须为目标结尾保留一个空字符。
        if (target_index + 1U >= capacity) return -1;
        target[target_index++] = (char)(uint8_t)output_byte;
    }
    // 写入稳定的 C 字符串结尾。
    target[target_index] = '\0';
    return 0;
}
