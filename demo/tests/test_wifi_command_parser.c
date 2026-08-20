// 引入被测 WiFi MQTT 命令解析器。
#include "../src/mqtt/internal/wifi_command_parser.h"

// 引入断言和字符串函数。
#include <assert.h>
#include <string.h>

// 验证完整 WiFi 配置能够解析并保留密码到受控内存结构。
static void test_profiles(void)
{
    // 构造后台保存后下发的正式配置。
    const char *payload = "{\"requestId\":\"req-1\",\"revision\":3,\"profiles\":[{\"profileId\":\"p-1\",\"ssid\":\"Office\",\"password\":\"password123\",\"priority\":80,\"enabled\":true}]}";
    workcard_wifi_profiles_command_t command;
    char error[128];
    assert(workcard_wifi_profiles_parse(payload, strlen(payload), &command, error, sizeof(error)) == 0);
    assert(command.profile_count == 1U);
    assert(strcmp(command.profiles[0].ssid, "Office") == 0);
    assert(strcmp(command.profiles[0].password, "password123") == 0);
}

// 验证空数组表示删除全部配置而不是格式错误。
static void test_empty_profiles(void)
{
    // 构造管理员清空 WiFi 列表的完整替换命令。
    const char *payload = "{\"requestId\":\"req-2\",\"revision\":4,\"profiles\":[]}";
    workcard_wifi_profiles_command_t command;
    char error[128];
    assert(workcard_wifi_profiles_parse(payload, strlen(payload), &command, error, sizeof(error)) == 0);
    assert(command.profile_count == 0U);
}

// 验证字符串中的右大括号不会被误判为对象结束。
static void test_brace_in_ssid(void)
{
    // 构造 SSID 含 JSON 保留字符的合法配置。
    const char *payload = "{\"requestId\":\"req-brace\",\"revision\":5,\"profiles\":[{\"profileId\":\"p-1\",\"ssid\":\"Office}WiFi\",\"password\":\"password123\",\"priority\":1,\"enabled\":true}]}";
    workcard_wifi_profiles_command_t command;
    char error[128];
    assert(workcard_wifi_profiles_parse(payload, strlen(payload), &command, error, sizeof(error)) == 0);
    assert(strcmp(command.profiles[0].ssid, "Office}WiFi") == 0);
}

// 验证非法控制字符不会进入状态文件字段。
static void test_control_character(void)
{
    // 构造包含换行转义的 SSID。
    const char *payload = "{\"requestId\":\"req-3\",\"revision\":1,\"profiles\":[{\"profileId\":\"p-1\",\"ssid\":\"Office\\nWiFi\",\"password\":\"password123\",\"priority\":1,\"enabled\":true}]}";
    workcard_wifi_profiles_command_t command;
    char error[128];
    assert(workcard_wifi_profiles_parse(payload, strlen(payload), &command, error, sizeof(error)) != 0);
}

// 运行 WiFi 命令解析器全部测试。
int main(void)
{
    // 依次执行正常、清空和安全边界测试。
    test_profiles();
    test_empty_profiles();
    test_brace_in_ssid();
    test_control_character();
    return 0;
}
