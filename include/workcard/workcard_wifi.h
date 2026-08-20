#ifndef WORKCARD_WIFI_H
#define WORKCARD_WIFI_H

// 引入公共类型和固定文本容量。
#include "workcard_types.h"

// 允许公开头文件同时被 C 和 C++ 客户程序引用。
#ifdef __cplusplus
extern "C" {
#endif

// 前置声明 SDK 不透明句柄。
typedef struct workcard_sdk workcard_sdk_t;

// 定义一次扫描最多返回的无线网络数量。
#define WORKCARD_WIFI_MAX_SCAN_RESULTS 32U

// 保存扫描到的一个无线网络。
typedef struct workcard_wifi_scan_result {
    // SDK 填写当前结构体大小。
    uint32_t struct_size;

    // 保存 UTF-8 SSID。
    char ssid[WORKCARD_TEXT_LENGTH];

    // 保存接入点 BSSID。
    char bssid[WORKCARD_ADDRESS_LENGTH];

    // 保存接收信号强度，单位为 dBm。
    int32_t rssi_dbm;

    // 非零表示网络要求 WPA/WPA2 密钥。
    uint8_t secured;

    // 保存用于未来扩展的对齐空间。
    uint8_t reserved[3];
} workcard_wifi_scan_result_t;

// 保存 WiFi 当前连接状态。
typedef struct workcard_wifi_status {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 非零表示 wpa_supplicant 已经完成关联。
    uint8_t connected;

    // 保存用于未来扩展的对齐空间。
    uint8_t reserved[3];

    // 保存当前连接的 UTF-8 SSID。
    char ssid[WORKCARD_TEXT_LENGTH];

    // 保存当前接入点 BSSID。
    char bssid[WORKCARD_ADDRESS_LENGTH];

    // 保存当前 IPv4 地址。
    char ipv4_address[WORKCARD_TEXT_LENGTH];

    // 保存当前接收信号强度，单位为 dBm。
    int32_t rssi_dbm;
} workcard_wifi_status_t;

// 保存客户要求连接并持久化的 WPA2 网络。
typedef struct workcard_wifi_config {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 保存 UTF-8 SSID。
    char ssid[WORKCARD_TEXT_LENGTH];

    // 保存 WPA2 密码，SDK 不会在响应和日志中返回该字段。
    char password[WORKCARD_TEXT_LENGTH];

    // 保存网络选择优先级，数值越大优先级越高。
    uint32_t priority;
} workcard_wifi_config_t;

// 查询 WiFi 当前连接状态。
workcard_result_t workcard_wifi_get_status(workcard_sdk_t *sdk, workcard_wifi_status_t *status);

// 扫描附近无线网络并返回实际写入数量。
workcard_result_t workcard_wifi_scan(
    workcard_sdk_t *sdk,
    workcard_wifi_scan_result_t *results,
    uint32_t capacity,
    uint32_t *result_count);

// 保存并连接一个 WPA2 网络，密码只用于本次受控配置。
workcard_result_t workcard_wifi_connect(workcard_sdk_t *sdk, const workcard_wifi_config_t *config);

// 断开当前 WiFi 连接但保留已持久化网络。
workcard_result_t workcard_wifi_disconnect(workcard_sdk_t *sdk);

// 删除指定 SSID 的持久化网络。
workcard_result_t workcard_wifi_remove_network(workcard_sdk_t *sdk, const char *ssid);

// 结束 C ABI 声明区域。
#ifdef __cplusplus
}
#endif

#endif
