#ifndef WORKCARD_NETWORK_H
#define WORKCARD_NETWORK_H

// 引入公共类型和固定文本容量。
#include "workcard_types.h"

// 允许公开头文件同时被 C 和 C++ 客户程序引用。
#ifdef __cplusplus
extern "C" {
#endif

// 前置声明 SDK 不透明句柄。
typedef struct workcard_sdk workcard_sdk_t;

// 定义网络接口类型。
typedef enum workcard_network_type {
    // 表示核心板有线以太网接口。
    WORKCARD_NETWORK_ETHERNET = 1,

    // 表示无线局域网接口。
    WORKCARD_NETWORK_WIFI = 2
} workcard_network_type_t;

// 保存一个网络接口的当前状态。
typedef struct workcard_network_status {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 保存网络接口类型。
    workcard_network_type_t type;

    // 非零表示内核已经检测到物理或无线链路。
    uint8_t link_up;

    // 非零表示接口已经取得 IPv4 地址。
    uint8_t has_ipv4;

    // 保存用于未来扩展的对齐空间。
    uint8_t reserved[2];

    // 保存 Linux 网络接口名称。
    char interface_name[WORKCARD_TEXT_LENGTH];

    // 保存 IPv4 地址文本。
    char ipv4_address[WORKCARD_TEXT_LENGTH];

    // 保存默认网关地址文本。
    char gateway[WORKCARD_TEXT_LENGTH];

    // 保存当前主 DNS 地址文本。
    char dns_server[WORKCARD_TEXT_LENGTH];
} workcard_network_status_t;

// 查询指定网络接口的链路和 IPv4 状态。
workcard_result_t workcard_network_get_status(
    workcard_sdk_t *sdk,
    workcard_network_type_t type,
    workcard_network_status_t *status);

// 结束 C ABI 声明区域。
#ifdef __cplusplus
}
#endif

#endif
