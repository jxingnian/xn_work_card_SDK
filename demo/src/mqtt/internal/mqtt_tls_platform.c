#define _POSIX_C_SOURCE 200809L

#include "mqtt_tls_platform.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

// 引入 OpenSSL 错误队列接口，向设备日志输出可定位的 TLS 失败原因。
#include <openssl/err.h>

// 初始化倒计时对象。
void TimerInit(Timer *timer)
{
    // 清空结束时间。
    timer->end_time = (struct timeval){0, 0};
}

// 判断倒计时是否已经结束。
char TimerIsExpired(Timer *timer)
{
    // 读取当前实时时钟。
    struct timeval now;
    gettimeofday(&now, NULL);

    // 比较秒和微秒字段。
    return now.tv_sec > timer->end_time.tv_sec ||
        (now.tv_sec == timer->end_time.tv_sec && now.tv_usec >= timer->end_time.tv_usec);
}

// 按毫秒设置倒计时。
void TimerCountdownMS(Timer *timer, unsigned int timeout_ms)
{
    // 读取当前实时时钟。
    struct timeval now;
    gettimeofday(&now, NULL);

    // 累加完整秒数。
    timer->end_time.tv_sec = now.tv_sec + (time_t)(timeout_ms / 1000U);

    // 累加剩余微秒数。
    timer->end_time.tv_usec = now.tv_usec + (suseconds_t)((timeout_ms % 1000U) * 1000U);

    // 处理微秒字段进位。
    if (timer->end_time.tv_usec >= 1000000) {
        timer->end_time.tv_sec += 1;
        timer->end_time.tv_usec -= 1000000;
    }
}

// 按秒设置倒计时。
void TimerCountdown(Timer *timer, unsigned int timeout_seconds)
{
    // 将秒转换为毫秒并复用统一逻辑。
    TimerCountdownMS(timer, timeout_seconds * 1000U);
}

// 返回倒计时剩余毫秒数。
int TimerLeftMS(Timer *timer)
{
    // 已经过期时返回零。
    if (TimerIsExpired(timer)) {
        return 0;
    }

    // 读取当前实时时钟。
    struct timeval now;
    gettimeofday(&now, NULL);

    // 计算剩余微秒数。
    long long remaining_us =
        (long long)(timer->end_time.tv_sec - now.tv_sec) * 1000000LL +
        (long long)(timer->end_time.tv_usec - now.tv_usec);

    // 返回向上取整后的毫秒数。
    return (int)((remaining_us + 999LL) / 1000LL);
}

// 等待套接字达到指定读写状态。
static int xn_mqtt_wait_socket(int socket_fd, int write_ready, int timeout_ms)
{
    // 初始化读取和写入集合。
    fd_set read_set;
    fd_set write_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);

    // 根据调用方向设置目标集合。
    if (write_ready) {
        FD_SET(socket_fd, &write_set);
    } else {
        FD_SET(socket_fd, &read_set);
    }

    // 将毫秒超时转换为 timeval。
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };

    // 等待套接字就绪或超时。
    return select(socket_fd + 1, &read_set, &write_set, NULL, &timeout);
}

// 从 TLS 会话读取 Paho 请求的数据。
static int xn_mqtt_tls_read(Network *network, unsigned char *buffer, int length, int timeout_ms)
{
    // 初始化累计读取长度。
    int total_read = 0;

    // 在超时前尽量读满 Paho 请求长度。
    while (total_read < length) {
        // OpenSSL 没有缓存数据时等待底层套接字。
        if (SSL_pending(network->ssl) == 0 && xn_mqtt_wait_socket(network->socket_fd, 0, timeout_ms) <= 0) {
            break;
        }

        // 读取当前数据片段。
        int result = SSL_read(network->ssl, buffer + total_read, length - total_read);

        // 正常读取时累加长度。
        if (result > 0) {
            total_read += result;
            continue;
        }

        // 获取 OpenSSL 错误类型。
        int ssl_error = SSL_get_error(network->ssl, result);

        // 可重试错误交给下一轮等待。
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            continue;
        }

        // TLS 关闭在未读取任何数据时返回零。
        return total_read > 0 ? total_read : 0;
    }

    // 返回实际读取长度。
    return total_read;
}

// 向 TLS 会话完整写入 Paho 数据。
static int xn_mqtt_tls_write(Network *network, unsigned char *buffer, int length, int timeout_ms)
{
    // 初始化累计写入长度。
    int total_written = 0;

    // 循环直到写出完整 MQTT 包。
    while (total_written < length) {
        // 等待底层套接字可写。
        if (xn_mqtt_wait_socket(network->socket_fd, 1, timeout_ms) <= 0) {
            break;
        }

        // 写入当前数据片段。
        int result = SSL_write(network->ssl, buffer + total_written, length - total_written);

        // 正常写入时累加长度。
        if (result > 0) {
            total_written += result;
            continue;
        }

        // 获取 OpenSSL 错误类型。
        int ssl_error = SSL_get_error(network->ssl, result);

        // 可重试错误交给下一轮等待。
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            continue;
        }

        // 不可恢复错误返回失败。
        return -1;
    }

    // 返回实际写入长度。
    return total_written;
}

// 初始化 TLS 网络结构。
void xn_mqtt_network_init(Network *network)
{
    // 清空全部连接字段。
    memset(network, 0, sizeof(*network));

    // 使用负一标记无效套接字。
    network->socket_fd = -1;

    // 注册 Paho 所需的 TLS 读取函数。
    network->mqttread = xn_mqtt_tls_read;

    // 注册 Paho 所需的 TLS 写入函数。
    network->mqttwrite = xn_mqtt_tls_write;
}

// 建立 TCP 连接。
static int xn_mqtt_connect_tcp(const char *host, unsigned short port)
{
    // 将端口转换为十进制文本。
    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port);

    // 配置地址查询参数。
    const struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
    };

    // 查询主机全部地址。
    struct addrinfo *addresses = NULL;
    if (getaddrinfo(host, port_text, &hints, &addresses) != 0) {
        return -1;
    }

    // 初始化连接套接字为无效值。
    int socket_fd = -1;

    // 逐个尝试解析得到的地址。
    for (struct addrinfo *address = addresses; address != NULL; address = address->ai_next) {
        // 创建当前地址族套接字。
        socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);

        // 创建失败时继续下一个地址。
        if (socket_fd < 0) {
            continue;
        }

        // 连接成功时停止遍历。
        if (connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }

        // 连接失败时关闭当前套接字。
        close(socket_fd);
        socket_fd = -1;
    }

    // 释放地址查询结果。
    freeaddrinfo(addresses);

    // 返回已连接套接字或失败值。
    return socket_fd;
}

// 建立并校验 MQTT TLS 连接。
int xn_mqtt_network_connect(
    Network *network,
    const char *host,
    unsigned short port,
    const char *ca_file,
    char *error_message,
    size_t error_message_length)
{
    // 校验调用参数。
    if (network == NULL || host == NULL || ca_file == NULL) {
        return -1;
    }

    // 创建 TLS 客户端上下文。
    network->ssl_context = SSL_CTX_new(TLS_client_method());
    if (network->ssl_context == NULL) {
        snprintf(error_message, error_message_length, "MQTT TLS 上下文创建失败");
        return -1;
    }

    // 启用服务器证书校验。
    SSL_CTX_set_verify(network->ssl_context, SSL_VERIFY_PEER, NULL);

    // 加载生产 CA 证书。
    if (SSL_CTX_load_verify_locations(network->ssl_context, ca_file, NULL) != 1) {
        snprintf(error_message, error_message_length, "MQTT CA 证书加载失败");
        xn_mqtt_network_disconnect(network);
        return -1;
    }

    // 建立底层 TCP 连接。
    network->socket_fd = xn_mqtt_connect_tcp(host, port);
    if (network->socket_fd < 0) {
        snprintf(error_message, error_message_length, "MQTT TCP 连接失败");
        xn_mqtt_network_disconnect(network);
        return -1;
    }

    // 创建当前 TLS 会话。
    network->ssl = SSL_new(network->ssl_context);
    if (network->ssl == NULL) {
        snprintf(error_message, error_message_length, "MQTT TLS 会话创建失败");
        xn_mqtt_network_disconnect(network);
        return -1;
    }

    // 设置 TLS SNI 主机名。
    if (SSL_set_tlsext_host_name(network->ssl, host) != 1 || SSL_set1_host(network->ssl, host) != 1) {
        snprintf(error_message, error_message_length, "MQTT TLS 主机名校验设置失败");
        xn_mqtt_network_disconnect(network);
        return -1;
    }

    // 绑定 TLS 会话和 TCP 套接字。
    SSL_set_fd(network->ssl, network->socket_fd);

    // 执行 TLS 握手并保存返回值，避免重复调用改变 OpenSSL 错误队列。
    const int ssl_connect_result = SSL_connect(network->ssl);

    // 读取证书链校验结果，即使握手失败也保留可诊断的验证状态。
    const long verify_result = SSL_get_verify_result(network->ssl);

    // 握手或证书校验失败时返回 OpenSSL 原始错误，便于区分时间、证书链和协议问题。
    if (ssl_connect_result != 1 || verify_result != X509_V_OK) {
        // 读取当前线程错误队列中的最后一个 OpenSSL 错误码。
        const unsigned long openssl_error = ERR_peek_last_error();

        // 使用固定长度缓冲区保存不包含凭据的 OpenSSL 错误文本。
        char openssl_error_text[256] = "no OpenSSL error detail";

        // 错误队列非空时将错误码转换为稳定的可读文本。
        if (openssl_error != 0UL) {
            ERR_error_string_n(openssl_error, openssl_error_text, sizeof(openssl_error_text));
        }

        // 同时输出握手结果和证书验证结果，禁止再次把所有 TLS 故障合并成模糊错误。
        snprintf(
            error_message,
            error_message_length,
            "MQTT TLS 握手失败: ssl=%d verify=%ld(%s) openssl=%s",
            ssl_connect_result,
            verify_result,
            X509_verify_cert_error_string(verify_result),
            openssl_error_text);
        xn_mqtt_network_disconnect(network);
        return -1;
    }

    // 返回连接成功。
    return 0;
}

// 关闭 TLS 和 TCP 连接。
void xn_mqtt_network_disconnect(Network *network)
{
    // 空网络对象无需处理。
    if (network == NULL) {
        return;
    }

    // 释放 TLS 会话。
    if (network->ssl != NULL) {
        SSL_shutdown(network->ssl);
        SSL_free(network->ssl);
        network->ssl = NULL;
    }

    // 释放 TLS 上下文。
    if (network->ssl_context != NULL) {
        SSL_CTX_free(network->ssl_context);
        network->ssl_context = NULL;
    }

    // 关闭 TCP 套接字。
    if (network->socket_fd >= 0) {
        close(network->socket_fd);
        network->socket_fd = -1;
    }
}
