#ifndef XN_MQTT_TLS_PLATFORM_H
#define XN_MQTT_TLS_PLATFORM_H

#include <openssl/ssl.h>

#include <stddef.h>

// 提供 Paho MQTTClient.c 使用的 strcmp 等标准字符串函数声明。
#include <string.h>

#include <sys/time.h>

// 保存 Paho Embedded C 使用的倒计时状态。
typedef struct Timer {
    // 保存倒计时结束的绝对时间。
    struct timeval end_time;
} Timer;

// 初始化倒计时对象。
void TimerInit(Timer *timer);

// 判断倒计时是否已经结束。
char TimerIsExpired(Timer *timer);

// 按毫秒设置倒计时。
void TimerCountdownMS(Timer *timer, unsigned int timeout_ms);

// 按秒设置倒计时。
void TimerCountdown(Timer *timer, unsigned int timeout_seconds);

// 返回倒计时剩余毫秒数。
int TimerLeftMS(Timer *timer);

// 保存 Paho MQTT 客户端使用的 TLS 网络连接。
typedef struct Network {
    // 保存 TCP 套接字。
    int socket_fd;

    // 保存 OpenSSL 上下文。
    SSL_CTX *ssl_context;

    // 保存 OpenSSL 会话。
    SSL *ssl;

    // 保存 Paho 调用的读取函数。
    int (*mqttread)(struct Network *network, unsigned char *buffer, int length, int timeout_ms);

    // 保存 Paho 调用的写入函数。
    int (*mqttwrite)(struct Network *network, unsigned char *buffer, int length, int timeout_ms);
} Network;

// 初始化 TLS 网络结构。
void xn_mqtt_network_init(Network *network);

// 建立并校验 MQTT TLS 连接。
int xn_mqtt_network_connect(
    Network *network,
    const char *host,
    unsigned short port,
    const char *ca_file,
    char *error_message,
    size_t error_message_length);

// 关闭 TLS 和 TCP 连接。
void xn_mqtt_network_disconnect(Network *network);

#endif
