#ifndef WORKCARD_WIFI_COMMAND_PARSER_H
#define WORKCARD_WIFI_COMMAND_PARSER_H

// 引入固定整数和字符串类型。
#include <stddef.h>

// 定义设备端最多处理的 WiFi profile 数量。
#define WORKCARD_WIFI_COMMAND_MAX_PROFILES 8U

// 保存一个经过 JSON 校验的 WiFi profile。
typedef struct {
    // 保存后台 profile 标识。
    char profile_id[96];
    // 保存 UTF-8 SSID。
    char ssid[128];
    // 保存短时 MQTT 明文密码，仅驻留内存。
    char password[128];
    // 保存连接优先级。
    unsigned int priority;
    // 保存是否启用。
    int enabled;
} workcard_wifi_command_profile_t;

// 保存 profiles/set 命令解析结果。
typedef struct {
    // 保存请求编号。
    char request_id[96];
    // 保存 WiFi 配置 revision。
    unsigned int revision;
    // 保存解析出的 profile 数量。
    unsigned int profile_count;
    // 保存有限 profile 数组。
    workcard_wifi_command_profile_t profiles[WORKCARD_WIFI_COMMAND_MAX_PROFILES];
} workcard_wifi_profiles_command_t;

// 保存 WiFi 扫描或连接动作命令解析结果。
typedef struct {
    // 保存请求编号。
    char request_id[96];
    // 保存动作名称。
    char action[32];
    // 保存 profile 标识。
    char profile_id[96];
    // 保存 SSID。
    char ssid[128];
    // 保存短时密码。
    char password[128];
    // 保存连接优先级。
    unsigned int priority;
} workcard_wifi_action_command_t;

// 解析完整 WiFi profile 配置命令。
int workcard_wifi_profiles_parse(const char *payload, size_t length, workcard_wifi_profiles_command_t *command, char *error, size_t error_length);

// 解析 WiFi 扫描命令，仅读取请求编号。
int workcard_wifi_scan_command_parse(const char *payload, size_t length, workcard_wifi_action_command_t *command, char *error, size_t error_length);

// 解析 WiFi 连接动作命令。
int workcard_wifi_action_parse(const char *payload, size_t length, workcard_wifi_action_command_t *command, char *error, size_t error_length);

#endif
