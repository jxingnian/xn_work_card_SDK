// 引入 WiFi 命令解析公开接口。
#include "wifi_command_parser.h"

// 引入标准字符、数字和内存接口。
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 写入统一的解析错误文本。
static int wifi_parser_error(char *error, size_t length, const char *message)
{
    // 错误缓冲区为空时只返回失败。
    if (error != NULL && length > 0U) (void)snprintf(error, length, "%s", message);
    return -1;
}

// 跳过 JSON 空白字符。
static const char *wifi_skip_space(const char *cursor, const char *end)
{
    // 仅跳过标准 ASCII JSON 空白。
    while (cursor < end && isspace((unsigned char)*cursor)) cursor++;
    return cursor;
}

// 在对象范围内查找键并定位到值起点。
static const char *wifi_find_value(const char *begin, const char *end, const char *key)
{
    // 构造严格带引号的 JSON 键。
    char quoted_key[128];
    (void)snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key);
    const size_t key_length = strlen(quoted_key);
    const char *cursor = begin;
    while (cursor + key_length < end) {
        const char *found = strstr(cursor, quoted_key);
        if (found == NULL || found + key_length >= end) return NULL;
        const char *separator = wifi_skip_space(found + key_length, end);
        if (separator < end && *separator == ':') return wifi_skip_space(separator + 1U, end);
        cursor = found + key_length;
    }
    return NULL;
}

// 解析 JSON 字符串并处理常用转义。
static int wifi_parse_string(const char *value, const char *end, char *output, size_t capacity, const char **after)
{
    // 字符串必须以双引号开始。
    if (value == NULL || value >= end || *value != '"' || capacity == 0U) return -1;
    size_t written = 0U;
    const char *cursor = value + 1U;
    while (cursor < end && *cursor != '"') {
        unsigned char character = (unsigned char)*cursor++;
        if (character == '\\') {
            if (cursor >= end) return -1;
            character = (unsigned char)*cursor++;
            if (character == 'n') character = '\n';
            else if (character == 'r') character = '\r';
            else if (character == 't') character = '\t';
            else if (character != '"' && character != '\\' && character != '/') return -1;
        }
        if (written + 1U >= capacity) return -1;
        output[written++] = (char)character;
    }
    if (cursor >= end || *cursor != '"') return -1;
    output[written] = '\0';
    if (after != NULL) *after = cursor + 1U;
    return 0;
}

// 解析无符号整数值。
static int wifi_parse_uint(const char *value, const char *end, unsigned int *output)
{
    // 使用受限临时文本避免读取边界外内容。
    char number[32];
    size_t length = 0U;
    while (value < end && isdigit((unsigned char)*value) && length + 1U < sizeof(number)) number[length++] = *value++;
    number[length] = '\0';
    if (length == 0U || (value < end && !isspace((unsigned char)*value) && *value != ',' && *value != '}')) return -1;
    char *tail = NULL;
    unsigned long parsed = strtoul(number, &tail, 10);
    if (*tail != '\0' || parsed > 0xFFFFFFFFUL) return -1;
    *output = (unsigned int)parsed;
    return 0;
}

// 读取对象中的字符串字段。
static int wifi_object_string(const char *begin, const char *end, const char *key, char *output, size_t capacity)
{
    const char *value = wifi_find_value(begin, end, key);
    return wifi_parse_string(value, end, output, capacity, NULL);
}

// 读取对象中的无符号字段。
static int wifi_object_uint(const char *begin, const char *end, const char *key, unsigned int *output)
{
    const char *value = wifi_find_value(begin, end, key);
    return value == NULL ? -1 : wifi_parse_uint(value, end, output);
}

// 读取对象中的布尔字段。
static int wifi_object_bool(const char *begin, const char *end, const char *key, int *output)
{
    const char *value = wifi_find_value(begin, end, key);
    if (value == NULL) return -1;
    if (value + 4U <= end && strncmp(value, "true", 4U) == 0) *output = 1;
    else if (value + 5U <= end && strncmp(value, "false", 5U) == 0) *output = 0;
    else return -1;
    return 0;
}

// 在 JSON 对象中跳过字符串内容和转义字符，定位对象真正的结束大括号。
static const char *wifi_find_object_end(const char *object_begin, const char *end)
{
    // 当前对象从调用方确认的左大括号之后开始扫描。
    int in_string = 0;
    int escaped = 0;
    for (const char *cursor = object_begin; cursor < end; ++cursor) {
        const char character = *cursor;
        if (escaped != 0) {
            escaped = 0;
            continue;
        }
        if (in_string != 0 && character == '\\') {
            escaped = 1;
            continue;
        }
        if (character == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string == 0 && character == '}') return cursor;
    }
    // 未找到完整对象时返回空指针。
    return NULL;
}

// 解析公共请求编号字段。
static int wifi_parse_request_id(const char *payload, const char *end, char *request_id, size_t capacity, char *error, size_t error_length)
{
    if (wifi_object_string(payload, end, "requestId", request_id, capacity) != 0 || request_id[0] == '\0') return wifi_parser_error(error, error_length, "WiFi 命令缺少 requestId");
    return 0;
}

// 解析完整 profiles/set 命令。
int workcard_wifi_profiles_parse(const char *payload, size_t length, workcard_wifi_profiles_command_t *command, char *error, size_t error_length)
{
    if (payload == NULL || command == NULL || length == 0U) return wifi_parser_error(error, error_length, "WiFi 配置命令为空");
    memset(command, 0, sizeof(*command));
    const char *end = payload + length;
    if (wifi_parse_request_id(payload, end, command->request_id, sizeof(command->request_id), error, error_length) != 0 ||
        wifi_object_uint(payload, end, "revision", &command->revision) != 0) return wifi_parser_error(error, error_length, "WiFi 配置缺少有效 revision");
    const char *array = wifi_find_value(payload, end, "profiles");
    if (array == NULL || *array != '[') return wifi_parser_error(error, error_length, "WiFi 配置缺少 profiles 数组");
    const char *cursor = array + 1U;
    while (cursor < end && *cursor != ']') {
        cursor = wifi_skip_space(cursor, end);
        if (cursor >= end || *cursor != '{' || command->profile_count >= WORKCARD_WIFI_COMMAND_MAX_PROFILES) return wifi_parser_error(error, error_length, "WiFi profile 数量或格式错误");
        const char *object_begin = cursor++;
        const char *object_end = wifi_find_object_end(cursor, end);
        if (object_end == NULL) return wifi_parser_error(error, error_length, "WiFi profile 对象不完整");
        workcard_wifi_command_profile_t *profile = &command->profiles[command->profile_count];
        if (wifi_object_string(object_begin, object_end, "profileId", profile->profile_id, sizeof(profile->profile_id)) != 0 ||
            wifi_object_string(object_begin, object_end, "ssid", profile->ssid, sizeof(profile->ssid)) != 0 ||
            wifi_object_string(object_begin, object_end, "password", profile->password, sizeof(profile->password)) != 0 ||
            wifi_object_uint(object_begin, object_end, "priority", &profile->priority) != 0 ||
            wifi_object_bool(object_begin, object_end, "enabled", &profile->enabled) != 0) return wifi_parser_error(error, error_length, "WiFi profile 字段格式错误");
        // SSID、密码和标识不得包含控制字符，避免破坏状态文件或日志格式。
        for (size_t character_index = 0U; profile->ssid[character_index] != '\0'; ++character_index) {
            if ((unsigned char)profile->ssid[character_index] < 0x20U || (unsigned char)profile->ssid[character_index] == 0x7FU) return wifi_parser_error(error, error_length, "WiFi SSID 包含控制字符");
        }
        for (size_t character_index = 0U; profile->profile_id[character_index] != '\0'; ++character_index) {
            if ((unsigned char)profile->profile_id[character_index] < 0x20U || (unsigned char)profile->profile_id[character_index] == 0x7FU) return wifi_parser_error(error, error_length, "WiFi profileId 包含控制字符");
        }
        for (size_t character_index = 0U; profile->password[character_index] != '\0'; ++character_index) {
            if ((unsigned char)profile->password[character_index] < 0x20U || (unsigned char)profile->password[character_index] == 0x7FU) return wifi_parser_error(error, error_length, "WiFi 密码包含控制字符");
        }
        if (profile->ssid[0] == '\0' || strlen(profile->ssid) > 32U || strlen(profile->password) < 8U || strlen(profile->password) > 63U || profile->priority > 100U) return wifi_parser_error(error, error_length, "WiFi profile 参数越界");
        command->profile_count++;
        cursor = object_end + 1U;
        cursor = wifi_skip_space(cursor, end);
        if (cursor < end && *cursor == ',') cursor++;
    }
    // 空数组表示管理员删除全部 WiFi 配置，属于合法的完整替换操作。
    return 0;
}

// 解析 WiFi 扫描命令。
int workcard_wifi_scan_command_parse(const char *payload, size_t length, workcard_wifi_action_command_t *command, char *error, size_t error_length)
{
    if (payload == NULL || command == NULL) return wifi_parser_error(error, error_length, "WiFi 扫描命令为空");
    memset(command, 0, sizeof(*command));
    return wifi_parse_request_id(payload, payload + length, command->request_id, sizeof(command->request_id), error, error_length);
}

// 解析指定 WiFi 连接动作命令。
int workcard_wifi_action_parse(const char *payload, size_t length, workcard_wifi_action_command_t *command, char *error, size_t error_length)
{
    if (payload == NULL || command == NULL) return wifi_parser_error(error, error_length, "WiFi 动作命令为空");
    memset(command, 0, sizeof(*command));
    const char *end = payload + length;
    if (wifi_parse_request_id(payload, end, command->request_id, sizeof(command->request_id), error, error_length) != 0 ||
        wifi_object_string(payload, end, "action", command->action, sizeof(command->action)) != 0 ||
        wifi_object_string(payload, end, "profileId", command->profile_id, sizeof(command->profile_id)) != 0 ||
        wifi_object_string(payload, end, "ssid", command->ssid, sizeof(command->ssid)) != 0 ||
        wifi_object_string(payload, end, "password", command->password, sizeof(command->password)) != 0 ||
        wifi_object_uint(payload, end, "priority", &command->priority) != 0) return wifi_parser_error(error, error_length, "WiFi 连接动作字段格式错误");
    for (size_t character_index = 0U; command->ssid[character_index] != '\0'; ++character_index) {
        if ((unsigned char)command->ssid[character_index] < 0x20U || (unsigned char)command->ssid[character_index] == 0x7FU) return wifi_parser_error(error, error_length, "WiFi SSID 包含控制字符");
    }
    for (size_t character_index = 0U; command->password[character_index] != '\0'; ++character_index) {
        if ((unsigned char)command->password[character_index] < 0x20U || (unsigned char)command->password[character_index] == 0x7FU) return wifi_parser_error(error, error_length, "WiFi 密码包含控制字符");
    }
    if (strcmp(command->action, "connect") != 0 || command->ssid[0] == '\0' || strlen(command->ssid) > 32U || strlen(command->password) < 8U || strlen(command->password) > 63U || command->priority > 100U) return wifi_parser_error(error, error_length, "WiFi 连接动作参数无效");
    return 0;
}
