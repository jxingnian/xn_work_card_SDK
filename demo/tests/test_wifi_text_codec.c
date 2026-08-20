// 引入 WiFi 扫描文本转换接口。
#include "../src/network/wifi_text_codec.h"

// 引入断言和字符串接口。
#include <assert.h>
#include <string.h>

// 验证中文 UTF-8 的十六进制转义能够还原。
static void test_utf8_escape(void)
{
    // 使用 ASCII 转义表示设备扫描服务返回的 UTF-8 文本。
    const char *source = "\\xe5\\xaf\\x8c\\xe8\\xb6\\xb3\\xe5\\xae\\x89\\xe5\\xba\\xb7";
    // 使用明确的 UTF-8 字节序列作为期望值，避免测试环境 locale 影响源码字面量。
    static const unsigned char expected[] = {0xe5, 0xaf, 0x8c, 0xe8, 0xb6, 0xb3, 0xe5, 0xae, 0x89, 0xe5, 0xba, 0xb7, 0x00};
    char target[128];
    assert(workcard_wifi_decode_scan_text(source, target, sizeof(target)) == 0);
    assert(memcmp(target, expected, sizeof(expected)) == 0);
}

// 验证普通英文 SSID 保持原样。
static void test_ascii_text(void)
{
    // 普通 ASCII SSID 不应发生任何变化。
    char target[64];
    assert(workcard_wifi_decode_scan_text("Office-WiFi", target, sizeof(target)) == 0);
    assert(strcmp(target, "Office-WiFi") == 0);
}

// 运行 WiFi 文本转换测试。
int main(void)
{
    // 执行 UTF-8 和 ASCII 两类边界测试。
    test_utf8_escape();
    test_ascii_text();
    return 0;
}
