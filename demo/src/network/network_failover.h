#ifndef WORKCARD_NETWORK_FAILOVER_H
#define WORKCARD_NETWORK_FAILOVER_H

// 引入 Demo 配置结构。
#include "cloud/internal/config.h"

// 保存切换前的 WiFi 默认路由和 DNS 文件内容。
typedef struct {
    // 保存切换前的默认路由文本。
    char wifi_route[256];
    // 保存切换前的 resolv.conf 内容。
    char resolv_conf[1024];
    // 标记是否成功保存了原始网络状态。
    int saved;
} workcard_network_failover_t;

// 切换到有线网络并保存原始 WiFi 网络状态。
int workcard_network_failover_enter(const xn_app_config *config, workcard_network_failover_t *state);

// 恢复切换前的 WiFi 默认路由和 DNS。
void workcard_network_failover_leave(const xn_app_config *config, workcard_network_failover_t *state);

#endif
