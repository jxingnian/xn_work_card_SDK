// 引入网络切换公开接口。
#include "network_failover.h"

// 引入固定命令、文件和字符串接口。
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// 校验网络接口名只包含 Linux 接口允许的安全字符。
static int workcard_network_interface_is_valid(const char *interface_name)
{
    // 空接口名不能参与系统命令。
    if (interface_name == NULL || interface_name[0] == '\0') return 0;
    // 限制长度并逐字符拒绝空格和 shell 元字符。
    size_t length = 0U;
    while (interface_name[length] != '\0') {
        const unsigned char character = (unsigned char)interface_name[length];
        if (!isalnum(character) && character != '_' && character != '-' && character != '.' && character != ':') return 0;
        length++;
        if (length > 15U) return 0;
    }
    // 返回接口名合法。
    return 1;
}

// 校验网关是规范 IPv4 地址，防止配置文本进入 shell 语法。
static int workcard_network_ipv4_is_valid(const char *address)
{
    // 使用系统 IPv4 解析器执行严格格式校验。
    struct in_addr parsed_address;
    return address != NULL && inet_pton(AF_INET, address, &parsed_address) == 1;
}

// 删除命令输出尾部换行，保证路由文本可以安全恢复。
static void workcard_network_trim_line(char *text)
{
    // 从末尾移除 CR 和 LF。
    size_t length = text == NULL ? 0U : strlen(text);
    while (length > 0U && (text[length - 1U] == '\n' || text[length - 1U] == '\r')) text[--length] = '\0';
}

// 通过指定有线接口验证 MQTT Broker 的实际 TCP 端口可达。
static int workcard_network_verify_cloud(const xn_app_config *config)
{
    // 使用系统解析器获得 MQTT 主机的 IPv4 地址。
    char service[16];
    (void)snprintf(service, sizeof(service), "%u", config->mqtt_port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = NULL;
    if (getaddrinfo(config->mqtt_host, service, &hints, &addresses) != 0 || addresses == NULL) return -1;
    // 将解析结果转换为只含数字和点的安全文本。
    const struct sockaddr_in *broker_address = (const struct sockaddr_in *)addresses->ai_addr;
    char broker_ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &broker_address->sin_addr, broker_ip, sizeof(broker_ip)) == NULL) {
        freeaddrinfo(addresses);
        return -1;
    }
    // 只为验证连接添加一个 Broker 主机路由，不影响现有 MQTT 默认路由。
    char route_command[512];
    const int route_length = snprintf(route_command, sizeof(route_command), "ip route replace %.15s/32 via %.15s dev %.15s >/tmp/workcard-network-probe.log 2>&1", broker_ip, config->wired_gateway, config->wired_interface);
    if (route_length <= 0 || (size_t)route_length >= sizeof(route_command) || system(route_command) != 0) {
        freeaddrinfo(addresses);
        return -1;
    }
    // 从指定接口建立一个三秒超时的 TCP 探测连接。
    const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    int connected = socket_fd >= 0 &&
        setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, config->wired_interface, strlen(config->wired_interface) + 1U) == 0 &&
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
        connect(socket_fd, addresses->ai_addr, addresses->ai_addrlen) == 0;
    if (socket_fd >= 0) (void)close(socket_fd);
    freeaddrinfo(addresses);
    // 无论探测结果如何都删除临时主机路由。
    (void)snprintf(route_command, sizeof(route_command), "ip route del %.15s/32 dev %.15s 2>/dev/null", broker_ip, config->wired_interface);
    (void)system(route_command);
    // 返回实际云端端口验证结果。
    return connected ? 0 : -1;
}

// 读取一个小型文本文件到固定缓冲区。
static int workcard_network_read_file(const char *path, char *buffer, size_t capacity)
{
    // 打开系统网络状态文件。
    FILE *file = fopen(path, "r");
    if (file == NULL) return -1;
    // 读取有限长度并保证结尾存在空字符。
    const size_t length = fread(buffer, 1U, capacity - 1U, file);
    buffer[length] = '\0';
    fclose(file);
    return 0;
}

// 切换到有线默认路由并使用有线网关 DNS。
int workcard_network_failover_enter(const xn_app_config *config, workcard_network_failover_t *state)
{
    // 必须同时提供有效配置和状态输出。
    if (config == NULL || state == NULL ||
        !workcard_network_interface_is_valid(config->wired_interface) ||
        !workcard_network_ipv4_is_valid(config->wired_gateway)) return -1;
    memset(state, 0, sizeof(*state));
    // 先验证有线接口已经存在且网关可达，失败时不触碰当前 WiFi 路由。
    char command[512];
    const int check_length = snprintf(command, sizeof(command), "ip link show dev %.15s >/dev/null 2>&1 && ping -I %.15s -c 1 -W 2 %.15s >/dev/null 2>&1", config->wired_interface, config->wired_interface, config->wired_gateway);
    if (check_length <= 0 || (size_t)check_length >= sizeof(command) || system(command) != 0) return -1;
    // 有线网关可达后继续验证实际 MQTT 云端端口。
    if (workcard_network_verify_cloud(config) != 0) return -1;
    // 保存当前 DNS 和 WiFi 默认路由，成功后原样恢复。
    (void)workcard_network_read_file("/etc/resolv.conf", state->resolv_conf, sizeof(state->resolv_conf));
    FILE *route = popen("ip route show default dev wlan0 2>/dev/null", "r");
    if (route != NULL) {
        (void)fgets(state->wifi_route, sizeof(state->wifi_route), route);
        (void)pclose(route);
    }
    // 清理路由命令输出结尾，避免恢复命令被换行截断。
    workcard_network_trim_line(state->wifi_route);
    state->saved = 1;
    // 使用受控配置字段切换默认路由，不拼接任何外部 MQTT 数据。
    const int route_length = snprintf(command, sizeof(command), "ip route replace default via %.15s dev %.15s metric 10 >/tmp/workcard-network-failover.log 2>&1", config->wired_gateway, config->wired_interface);
    if (route_length <= 0 || (size_t)route_length >= sizeof(command) || system(command) != 0) {
        state->saved = 0;
        return -1;
    }
    // 写入有线 DNS，保证 MQTT/WSS 重连时可以解析云端地址。
    FILE *dns = fopen("/etc/resolv.conf", "w");
    if (dns != NULL) {
        (void)fprintf(dns, "nameserver %.15s\n", config->wired_gateway);
        (void)fclose(dns);
    }
    return 0;
}

// 恢复切换前的 WiFi 默认路由和 DNS。
void workcard_network_failover_leave(const xn_app_config *config, workcard_network_failover_t *state)
{
    // 配置参数在当前恢复流程中不需要直接使用。
    // 没有成功进入备用网络时不修改系统状态。
    if (state == NULL || state->saved == 0) return;
    // 删除有线默认路由并恢复 WiFi 路由文本。
    char delete_command[384];
    (void)snprintf(delete_command, sizeof(delete_command), "ip route del default dev %.15s 2>/dev/null", config->wired_interface);
    (void)system(delete_command);
    if (state->wifi_route[0] != '\0') {
        char command[512];
        (void)snprintf(command, sizeof(command), "ip route replace %s >/tmp/workcard-network-restore.log 2>&1", state->wifi_route);
        (void)system(command);
    }
    // 恢复切换前 DNS；写入失败时保留系统现有 DNS。
    FILE *dns = fopen("/etc/resolv.conf", "w");
    if (dns != NULL) {
        (void)fputs(state->resolv_conf, dns);
        (void)fclose(dns);
    }
    state->saved = 0;
}
