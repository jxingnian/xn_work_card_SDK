#ifndef WORKCARD_WIFI_PROFILE_STORE_H
#define WORKCARD_WIFI_PROFILE_STORE_H

// 引入 SDK WiFi 接口和已经完成校验的 MQTT 命令结构。
#include "workcard/workcard_sdk.h"
#include "workcard/workcard_wifi.h"
#include "mqtt/internal/wifi_command_parser.h"

// 将后台完整配置清单同步到 Demo 状态，并删除已取消或禁用的旧网络。
int workcard_wifi_profile_store_apply(
    workcard_sdk_t *sdk,
    const char *state_path,
    const workcard_wifi_profiles_command_t *command,
    char *error,
    size_t error_length);

#endif
