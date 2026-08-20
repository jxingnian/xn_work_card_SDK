#ifndef XINGNIAN_WEBSOCKET_CLIENT_H
#define XINGNIAN_WEBSOCKET_CLIENT_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include <openssl/ssl.h>

// 定义收到服务器文本消息时的回调。
typedef void (*xn_ws_text_callback)(void *user_data, const char *message, size_t message_length);

// 定义收到服务器二进制消息时的回调。
typedef void (*xn_ws_binary_callback)(void *user_data, const uint8_t *payload, size_t payload_length);

// 保存一条加密 WebSocket 连接的全部运行状态。
typedef struct {
    // 保存 TLS 上下文。
    SSL_CTX *ssl_context;

    // 保存当前 TLS 会话。
    SSL *ssl;

    // 保存底层 TCP 套接字。
    int socket_fd;

    // 保存接收线程。
    pthread_t receiver_thread;

    // 保存发送互斥锁，防止音视频并发写入同一个 TLS 会话。
    pthread_mutex_t send_mutex;

    // 保存状态互斥锁。
    pthread_mutex_t state_mutex;

    // 标记接收线程是否需要继续运行。
    int running;

    // 标记连接是否可发送数据。
    int connected;

    // 保存服务器文本消息回调。
    xn_ws_text_callback text_callback;

    // 保存服务器二进制消息回调。
    xn_ws_binary_callback binary_callback;

    // 保存回调用户数据。
    void *callback_user_data;
} xn_ws_client;

// 初始化 WebSocket 客户端结构和线程同步对象。
int xn_ws_client_init(
    xn_ws_client *client,
    xn_ws_text_callback text_callback,
    xn_ws_binary_callback binary_callback,
    void *user_data);

// 建立并校验一条 WSS 连接。
int xn_ws_client_connect(xn_ws_client *client, const char *server_url, const char *ca_file, char *error_message, size_t error_message_length);

// 发送 UTF-8 文本 WebSocket 帧。
int xn_ws_client_send_text(xn_ws_client *client, const char *message, size_t message_length);

// 发送二进制 WebSocket 帧。
int xn_ws_client_send_binary(xn_ws_client *client, const uint8_t *payload, size_t payload_length);

// 查询当前连接是否仍可使用。
int xn_ws_client_is_connected(xn_ws_client *client);

// 关闭当前连接并等待接收线程退出。
void xn_ws_client_close(xn_ws_client *client);

// 销毁 WebSocket 客户端同步对象。
void xn_ws_client_destroy(xn_ws_client *client);

#endif
