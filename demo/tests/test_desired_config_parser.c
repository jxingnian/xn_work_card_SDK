// 引入被测 Desired 配置解析器。
#include "desired_config_parser.h"

// 引入断言和字符串函数。
#include <assert.h>
#include <string.h>

// 验证完整后台消息能够得到确定摄像头参数。
static void test_valid_desired(void)
{
    // 构造与后台正式协议一致的完整 Desired 消息。
    const char *payload =
        "{\"schemaVersion\":1,\"requestId\":\"req-1\",\"revision\":8,\"baseRevision\":7,"
        "\"config\":{\"project\":{\"projectId\":\"project-a\"},\"media\":{"
        "\"video\":{\"width\":800,\"height\":448,\"fps\":10,\"gop\":20,\"bitrateBps\":768000},"
        "\"audio\":{\"sampleRate\":16000,\"channels\":1}}}}";
    workcard_desired_config_t config;
    char error_message[128];
    assert(workcard_desired_config_parse(payload, strlen(payload), &config, error_message, sizeof(error_message)) == 0);
    assert(strcmp(config.request_id, "req-1") == 0);
    assert(config.revision == 8U);
    assert(config.base_revision == 7U);
    assert(config.video.width == 800U);
    assert(config.video.height == 448U);
    assert(config.video.fps == 10U);
    assert(config.video.gop == 20U);
    assert(config.video.bitrate_bps == 768000U);
}

// 验证奇数视频宽度会被当前硬件能力校验拒绝。
static void test_invalid_video_size(void)
{
    // 构造结构正确但宽度不合法的 Desired 消息。
    const char *payload =
        "{\"requestId\":\"req-2\",\"revision\":1,\"baseRevision\":0,\"config\":{\"media\":{"
        "\"video\":{\"width\":641,\"height\":320,\"fps\":5,\"gop\":10,\"bitrateBps\":512000}}}}";
    workcard_desired_config_t config;
    char error_message[128];
    assert(workcard_desired_config_parse(payload, strlen(payload), &config, error_message, sizeof(error_message)) != 0);
    assert(strstr(error_message, "能力范围") != NULL);
}

// 运行全部 Desired 配置解析测试。
int main(void)
{
    test_valid_desired();
    test_invalid_video_size();
    return 0;
}
