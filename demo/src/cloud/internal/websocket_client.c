#define _POSIX_C_SOURCE 200809L

#include "websocket_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509v3.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// 定义服务器控制消息允许的最大长度。
#define XN_WS_MAX_INCOMING_PAYLOAD 65536U

// 定义单个服务器地址允许的 TCP 建连上限，避免 Wi-Fi 丢包长期阻塞媒体清理。
#define XN_WS_TCP_CONNECT_TIMEOUT_MS 3000

// 定义 TLS 和 WebSocket 握手读写上限，保证停止命令能够在服务等待期内收敛。
#define XN_WS_SOCKET_IO_TIMEOUT_SECONDS 5

// 保存解析后的 WSS 地址字段。
typedef struct {
    // 保存服务器主机名。
    char host[256];

    // 保存服务器端口文本。
    char port[16];

    // 保存 WebSocket 请求路径。
    char path[512];
} xn_ws_url;

// 将连接状态更新为断开。
static void xn_ws_mark_disconnected(xn_ws_client *client)
{
    // 加锁保护共享连接状态。
    pthread_mutex_lock(&client->state_mutex);

    // 禁止后续媒体发送。
    client->connected = 0;

    // 解锁共享连接状态。
    pthread_mutex_unlock(&client->state_mutex);
}

// 解析当前版本支持的标准 WSS 地址。
static int xn_ws_parse_url(const char *server_url, xn_ws_url *parsed_url, char *error_message, size_t error_message_length)
{
    // 定义当前版本支持的地址前缀。
    const char *scheme = "wss://";

    // 拒绝非加密 WebSocket 地址。
    if (strncmp(server_url, scheme, strlen(scheme)) != 0) {
        snprintf(error_message, error_message_length, "服务器地址必须使用 wss://");
        return -1;
    }

    // 定位主机名起始位置。
    const char *authority_start = server_url + strlen(scheme);

    // 定位请求路径起始位置。
    const char *path_start = strchr(authority_start, '/');

    // 没有显式路径时使用根路径。
    if (path_start == NULL) {
        path_start = authority_start + strlen(authority_start);
    }

    // 计算主机和可选端口部分长度。
    size_t authority_length = (size_t)(path_start - authority_start);

    // 拒绝空主机名和过长主机名。
    if (authority_length == 0 || authority_length >= sizeof(parsed_url->host)) {
        snprintf(error_message, error_message_length, "服务器主机名长度不合法");
        return -1;
    }

    // 保存主机和可选端口部分以便继续拆分。
    char authority_buffer[256];
    memcpy(authority_buffer, authority_start, authority_length);
    authority_buffer[authority_length] = '\0';

    // 查找可选端口分隔符。
    char *port_separator = strrchr(authority_buffer, ':');

    // 保存显式端口或 WSS 默认端口。
    if (port_separator != NULL) {
        *port_separator = '\0';
        snprintf(parsed_url->port, sizeof(parsed_url->port), "%s", port_separator + 1);
    } else {
        snprintf(parsed_url->port, sizeof(parsed_url->port), "%s", "443");
    }

    // 保存服务器主机名。
    snprintf(parsed_url->host, sizeof(parsed_url->host), "%s", authority_buffer);

    // 保存请求路径并补充缺省根路径。
    snprintf(parsed_url->path, sizeof(parsed_url->path), "%s", *path_start == '\0' ? "/" : path_start);

    // 拒绝拆分后为空的主机名或端口。
    if (parsed_url->host[0] == '\0' || parsed_url->port[0] == '\0') {
        snprintf(error_message, error_message_length, "服务器地址缺少主机名或端口");
        return -1;
    }

    // 返回地址解析成功。
    return 0;
}

// 等待非阻塞 TCP 建连完成并取得内核返回的实际连接结果。
static int xn_ws_wait_for_tcp_connect(int socket_fd)
{
    // 只等待套接字变为可写，连接错误同样会通过可写事件返回。
    struct pollfd descriptor = {
        .fd = socket_fd,
        .events = POLLOUT,
        .revents = 0,
    };

    // 信号中断时使用剩余业务循环重新建连，不在这里无限等待。
    int poll_result = poll(&descriptor, 1, XN_WS_TCP_CONNECT_TIMEOUT_MS);
    if (poll_result <= 0) {
        // 超时使用标准错误码供上层输出确定原因。
        errno = poll_result == 0 ? ETIMEDOUT : errno;
        return -1;
    }

    // 读取 SO_ERROR 才能区分连接成功和异步连接失败。
    int socket_error = 0;
    socklen_t socket_error_length = sizeof(socket_error);
    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_length) != 0) {
        return -1;
    }

    // 内核返回非零错误时同步设置 errno 供调用方处理。
    if (socket_error != 0) {
        errno = socket_error;
        return -1;
    }

    // 套接字已经完成 TCP 三次握手。
    return 0;
}

// 为已连接套接字设置有限读写等待，覆盖 TLS 和 WebSocket 握手阶段。
static int xn_ws_set_socket_io_timeout(int socket_fd)
{
    // 使用相同上限约束接收和发送，避免任一方向永久阻塞。
    const struct timeval timeout = {
        .tv_sec = XN_WS_SOCKET_IO_TIMEOUT_SECONDS,
        .tv_usec = 0,
    };

    // 两个方向都设置成功才允许进入 TLS 握手。
    return setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
            setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0
        ? 0
        : -1;
}

// WebSocket 握手成功后清除接收超时，避免正常静默连接被误判为断线。
static int xn_ws_clear_socket_receive_timeout(int socket_fd)
{
    // 使用全零时间表示运行期接收允许持续阻塞等待服务器消息。
    const struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 0,
    };

    // 仅清除接收方向，发送方向继续保留有限等待保护。
    return setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

// 使用系统 DNS 解析并在有限时间内建立 TCP 连接。
static int xn_ws_connect_tcp(const xn_ws_url *parsed_url, char *error_message, size_t error_message_length)
{
    // 初始化地址查询参数。
    struct addrinfo query = {0};
    query.ai_family = AF_UNSPEC;
    query.ai_socktype = SOCK_STREAM;

    // 保存 DNS 查询结果链表。
    struct addrinfo *address_list = NULL;

    // 查询服务器全部可用地址。
    int query_result = getaddrinfo(parsed_url->host, parsed_url->port, &query, &address_list);

    // DNS 查询失败时返回标准错误文本。
    if (query_result != 0) {
        snprintf(error_message, error_message_length, "DNS 解析失败：%s", gai_strerror(query_result));
        return -1;
    }

    // 默认表示尚未建立连接。
    int socket_fd = -1;

    // 依次尝试 DNS 返回的全部地址。
    for (struct addrinfo *address = address_list; address != NULL; address = address->ai_next) {
        // 创建与当前地址匹配的 TCP 套接字。
        socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);

        // 套接字创建失败时继续尝试下一个地址。
        if (socket_fd < 0) {
            continue;
        }

        // 保存套接字原始状态并临时切换为非阻塞建连。
        int original_flags = fcntl(socket_fd, F_GETFL, 0);
        if (original_flags < 0 || fcntl(socket_fd, F_SETFL, original_flags | O_NONBLOCK) != 0) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }

        // 发起连接并接受立即成功或标准异步进行中状态。
        int connect_result = connect(socket_fd, address->ai_addr, address->ai_addrlen);
        if (connect_result != 0 && errno != EINPROGRESS && errno != EWOULDBLOCK) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }

        // 异步连接必须在三秒内完成，否则关闭本次套接字尝试下一个地址。
        if (connect_result != 0 && xn_ws_wait_for_tcp_connect(socket_fd) != 0) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }

        // TCP 完成后恢复阻塞模式，由套接字超时约束后续 TLS 读写。
        if (fcntl(socket_fd, F_SETFL, original_flags) != 0 || xn_ws_set_socket_io_timeout(socket_fd) != 0) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }

        // 当前服务器地址已经建立受超时保护的连接。
        break;
    }

    // 释放 DNS 查询结果链表。
    freeaddrinfo(address_list);

    // 所有地址均连接失败时返回错误。
    if (socket_fd < 0) {
        snprintf(error_message, error_message_length, "TCP 连接失败：%s", strerror(errno));
        return -1;
    }

    // 返回已连接的套接字。
    return socket_fd;
}

// 确保 TLS 数据被完整写出。
static int xn_ws_ssl_write_all(SSL *ssl, const uint8_t *data, size_t data_length)
{
    // 保存已经写出的字节数。
    size_t written_length = 0;

    // 循环写出全部数据。
    while (written_length < data_length) {
        // 计算当前调用允许写出的长度。
        size_t remaining_length = data_length - written_length;

        // OpenSSL 1.1 接口使用有符号整数长度。
        int write_length = remaining_length > 0x7FFFFFFFU ? 0x7FFFFFFF : (int)remaining_length;

        // 向 TLS 会话写入当前数据片段。
        int result = SSL_write(ssl, data + written_length, write_length);

        // TLS 写入失败时终止发送。
        if (result <= 0) {
            return -1;
        }

        // 累加本次成功写出的字节数。
        written_length += (size_t)result;
    }

    // 返回完整写入成功。
    return 0;
}

// 从 TLS 会话读取指定长度的数据。
static int xn_ws_ssl_read_all(SSL *ssl, uint8_t *data, size_t data_length)
{
    // 保存已经读取的字节数。
    size_t read_length = 0;

    // 循环读取完整数据。
    while (read_length < data_length) {
        // 计算当前调用允许读取的长度。
        size_t remaining_length = data_length - read_length;

        // OpenSSL 1.1 接口使用有符号整数长度。
        int request_length = remaining_length > 0x7FFFFFFFU ? 0x7FFFFFFF : (int)remaining_length;

        // 从 TLS 会话读取当前数据片段。
        int result = SSL_read(ssl, data + read_length, request_length);

        // 连接关闭或读取失败时终止接收。
        if (result <= 0) {
            return -1;
        }

        // 累加本次成功读取的字节数。
        read_length += (size_t)result;
    }

    // 返回完整读取成功。
    return 0;
}

// 生成 WebSocket 握手请求使用的随机密钥。
static int xn_ws_create_handshake_key(char *encoded_key, size_t encoded_key_length)
{
    // 保存协议要求的十六字节随机值。
    uint8_t random_key[16];

    // 使用 OpenSSL 安全随机源生成握手密钥。
    if (RAND_bytes(random_key, sizeof(random_key)) != 1) {
        return -1;
    }

    // Base64 后固定需要二十四字节和结尾空字符。
    if (encoded_key_length < 25) {
        return -1;
    }

    // 将随机值编码为标准 Base64 文本。
    int result = EVP_EncodeBlock((unsigned char *)encoded_key, random_key, sizeof(random_key));

    // Base64 编码失败时返回错误。
    if (result <= 0) {
        return -1;
    }

    // 补充字符串结尾空字符。
    encoded_key[result] = '\0';

    // 返回密钥生成成功。
    return 0;
}

// 根据客户端随机密钥计算服务器应返回的握手摘要。
static int xn_ws_create_expected_accept(const char *encoded_key, char *expected_accept, size_t expected_accept_length)
{
    // 定义 WebSocket 协议固定 GUID。
    const char *websocket_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    // 保存随机密钥与固定 GUID 的拼接结果。
    char source_text[128];

    // 拼接协议规定的摘要输入。
    int source_length = snprintf(source_text, sizeof(source_text), "%s%s", encoded_key, websocket_guid);

    // 拒绝被截断的摘要输入。
    if (source_length <= 0 || (size_t)source_length >= sizeof(source_text)) {
        return -1;
    }

    // 保存 SHA-1 摘要结果。
    unsigned char digest[SHA_DIGEST_LENGTH];

    // 计算协议规定的 SHA-1 摘要。
    SHA1((const unsigned char *)source_text, (size_t)source_length, digest);

    // Base64 后固定需要二十八字节和结尾空字符。
    if (expected_accept_length < 29) {
        return -1;
    }

    // 将 SHA-1 摘要编码为标准 Base64 文本。
    int result = EVP_EncodeBlock((unsigned char *)expected_accept, digest, sizeof(digest));

    // Base64 编码失败时返回错误。
    if (result <= 0) {
        return -1;
    }

    // 补充字符串结尾空字符。
    expected_accept[result] = '\0';

    // 返回摘要生成成功。
    return 0;
}

// 从 HTTP 响应中查找并校验指定头部值。
static int xn_ws_response_has_header(const char *response, const char *header_name, const char *expected_value)
{
    // 从首行之后开始逐行处理响应头。
    const char *line_start = strstr(response, "\r\n");

    // 缺少 HTTP 首行结尾时响应无效。
    if (line_start == NULL) {
        return 0;
    }

    // 跳过 HTTP 首行结尾。
    line_start += 2;

    // 保存待匹配头部名称长度。
    size_t header_name_length = strlen(header_name);

    // 逐行查找目标响应头。
    while (*line_start != '\0') {
        // 定位当前响应头行结尾。
        const char *line_end = strstr(line_start, "\r\n");

        // 响应头格式不完整时停止查找。
        if (line_end == NULL || line_end == line_start) {
            break;
        }

        // 检查当前响应头名称是否匹配。
        if ((size_t)(line_end - line_start) > header_name_length &&
            strncasecmp(line_start, header_name, header_name_length) == 0 &&
            line_start[header_name_length] == ':') {
            // 跳过头部名称、冒号和后续空白。
            const char *value_start = line_start + header_name_length + 1;
            while (value_start < line_end && (*value_start == ' ' || *value_start == '\t')) {
                value_start++;
            }

            // 去除头部值右侧空白。
            const char *value_end = line_end;
            while (value_end > value_start && (value_end[-1] == ' ' || value_end[-1] == '\t')) {
                value_end--;
            }

            // 比较实际头部值和协议期望值。
            size_t value_length = (size_t)(value_end - value_start);
            return strlen(expected_value) == value_length && strncmp(value_start, expected_value, value_length) == 0;
        }

        // 移动到下一条响应头。
        line_start = line_end + 2;
    }

    // 未找到目标响应头。
    return 0;
}

// 完成 HTTP Upgrade 握手并校验服务器摘要。
static int xn_ws_perform_handshake(xn_ws_client *client, const xn_ws_url *parsed_url, char *error_message, size_t error_message_length)
{
    // 保存客户端随机握手密钥。
    char encoded_key[32];

    // 生成随机握手密钥。
    if (xn_ws_create_handshake_key(encoded_key, sizeof(encoded_key)) != 0) {
        snprintf(error_message, error_message_length, "无法生成 WebSocket 握手密钥");
        return -1;
    }

    // 保存完整 HTTP Upgrade 请求。
    char request_buffer[2048];

    // 构建符合 RFC 6455 的客户端握手请求。
    int request_length = snprintf(
        request_buffer,
        sizeof(request_buffer),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: xingnian-device-stream/1.0\r\n"
        "\r\n",
        parsed_url->path,
        parsed_url->host,
        encoded_key);

    // 拒绝被截断的握手请求。
    if (request_length <= 0 || (size_t)request_length >= sizeof(request_buffer)) {
        snprintf(error_message, error_message_length, "WebSocket 握手请求过长");
        return -1;
    }

    // 发送完整 HTTP Upgrade 请求。
    if (xn_ws_ssl_write_all(client->ssl, (const uint8_t *)request_buffer, (size_t)request_length) != 0) {
        snprintf(error_message, error_message_length, "WebSocket 握手请求发送失败");
        return -1;
    }

    // 保存服务器 HTTP Upgrade 响应。
    char response_buffer[8192];

    // 保存当前已读取响应长度。
    size_t response_length = 0;

    // 逐字节读取到完整响应头，避免吞掉后续 WebSocket 帧。
    while (response_length + 1 < sizeof(response_buffer)) {
        // 读取一个响应字节。
        int result = SSL_read(client->ssl, response_buffer + response_length, 1);

        // TLS 读取失败时终止握手。
        if (result <= 0) {
            snprintf(error_message, error_message_length, "WebSocket 握手响应读取失败");
            return -1;
        }

        // 更新已读取响应长度。
        response_length += (size_t)result;

        // 补充字符串结尾空字符供文本查找使用。
        response_buffer[response_length] = '\0';

        // 收到空行时表示 HTTP 响应头完整。
        if (response_length >= 4 && strcmp(response_buffer + response_length - 4, "\r\n\r\n") == 0) {
            break;
        }
    }

    // 响应头超出限制时拒绝连接。
    if (response_length + 1 >= sizeof(response_buffer)) {
        snprintf(error_message, error_message_length, "WebSocket 握手响应头过长");
        return -1;
    }

    // 要求服务器明确返回 101 Switching Protocols。
    if (strncmp(response_buffer, "HTTP/1.1 101 ", 13) != 0 && strncmp(response_buffer, "HTTP/1.0 101 ", 13) != 0) {
        snprintf(error_message, error_message_length, "服务器拒绝 WebSocket Upgrade");
        return -1;
    }

    // 计算服务器应返回的 Sec-WebSocket-Accept。
    char expected_accept[32];

    // 生成握手摘要校验值。
    if (xn_ws_create_expected_accept(encoded_key, expected_accept, sizeof(expected_accept)) != 0) {
        snprintf(error_message, error_message_length, "无法生成 WebSocket 握手摘要");
        return -1;
    }

    // 校验服务器握手摘要，防止连接到非 WebSocket 服务。
    if (!xn_ws_response_has_header(response_buffer, "Sec-WebSocket-Accept", expected_accept)) {
        snprintf(error_message, error_message_length, "服务器 WebSocket 握手摘要校验失败");
        return -1;
    }

    // 返回握手成功。
    return 0;
}

// 发送一帧符合客户端掩码要求的 WebSocket 数据。
static int xn_ws_send_frame(xn_ws_client *client, uint8_t opcode, const uint8_t *payload, size_t payload_length)
{
    // 客户端数据帧固定使用四字节随机掩码。
    uint8_t mask_key[4];

    // 使用安全随机源生成帧掩码。
    if (RAND_bytes(mask_key, sizeof(mask_key)) != 1) {
        return -1;
    }

    // 根据负载长度确定扩展头部长度。
    size_t header_length = payload_length <= 125 ? 2 : (payload_length <= 0xFFFF ? 4 : 10);

    // 计算包含掩码的完整帧长度。
    size_t frame_length = header_length + sizeof(mask_key) + payload_length;

    // 为完整 WebSocket 帧分配连续内存。
    uint8_t *frame_buffer = malloc(frame_length);

    // 内存不足时返回失败。
    if (frame_buffer == NULL) {
        return -1;
    }

    // 设置 FIN 标志并写入帧操作码。
    frame_buffer[0] = (uint8_t)(0x80U | opcode);

    // 保存掩码字段写入位置。
    size_t mask_offset = 0;

    // 写入短长度格式。
    if (payload_length <= 125) {
        frame_buffer[1] = (uint8_t)(0x80U | payload_length);
        mask_offset = 2;
    } else if (payload_length <= 0xFFFF) {
        // 写入十六位扩展长度格式。
        frame_buffer[1] = 0x80U | 126U;
        frame_buffer[2] = (uint8_t)(payload_length >> 8);
        frame_buffer[3] = (uint8_t)payload_length;
        mask_offset = 4;
    } else {
        // 写入六十四位扩展长度格式。
        frame_buffer[1] = 0x80U | 127U;
        for (unsigned int byte_index = 0; byte_index < 8; byte_index++) {
            frame_buffer[2 + byte_index] = (uint8_t)((uint64_t)payload_length >> ((7U - byte_index) * 8U));
        }
        mask_offset = 10;
    }

    // 写入当前帧随机掩码。
    memcpy(frame_buffer + mask_offset, mask_key, sizeof(mask_key));

    // 对全部负载字节应用循环掩码。
    for (size_t payload_index = 0; payload_index < payload_length; payload_index++) {
        frame_buffer[mask_offset + sizeof(mask_key) + payload_index] = payload[payload_index] ^ mask_key[payload_index % 4];
    }

    // 加锁保证一帧数据不会被其他采集线程穿插写入。
    pthread_mutex_lock(&client->send_mutex);

    // 查询加锁后的实际连接状态。
    int can_send = xn_ws_client_is_connected(client);

    // 连接可用时发送完整帧。
    int send_result = can_send ? xn_ws_ssl_write_all(client->ssl, frame_buffer, frame_length) : -1;

    // 解锁发送互斥锁。
    pthread_mutex_unlock(&client->send_mutex);

    // 释放临时 WebSocket 帧缓冲区。
    free(frame_buffer);

    // 写入失败时立即禁止后续发送。
    if (send_result != 0) {
        xn_ws_mark_disconnected(client);
        if (client->socket_fd >= 0) {
            shutdown(client->socket_fd, SHUT_RDWR);
        }
        return -1;
    }

    // 返回帧发送成功。
    return 0;
}

// 接收并处理服务器发送的 WebSocket 帧。
static void *xn_ws_receiver_main(void *thread_argument)
{
    // 获取当前 WebSocket 客户端实例。
    xn_ws_client *client = thread_argument;

    // 持续接收服务器控制消息。
    while (xn_ws_client_is_connected(client)) {
        // 保存基础帧头。
        uint8_t frame_header[2];

        // 读取两字节基础帧头。
        if (xn_ws_ssl_read_all(client->ssl, frame_header, sizeof(frame_header)) != 0) {
            break;
        }

        // 提取是否为最终分片。
        int final_fragment = (frame_header[0] & 0x80U) != 0;

        // 提取帧操作码。
        uint8_t opcode = frame_header[0] & 0x0FU;

        // 提取服务器是否错误使用客户端掩码。
        int masked = (frame_header[1] & 0x80U) != 0;

        // 提取基础负载长度。
        uint64_t payload_length = frame_header[1] & 0x7FU;

        // 读取十六位扩展负载长度。
        if (payload_length == 126) {
            uint8_t extended_length[2];
            if (xn_ws_ssl_read_all(client->ssl, extended_length, sizeof(extended_length)) != 0) {
                break;
            }
            payload_length = ((uint64_t)extended_length[0] << 8) | extended_length[1];
        } else if (payload_length == 127) {
            // 读取六十四位扩展负载长度。
            uint8_t extended_length[8];
            if (xn_ws_ssl_read_all(client->ssl, extended_length, sizeof(extended_length)) != 0) {
                break;
            }
            payload_length = 0;
            for (unsigned int byte_index = 0; byte_index < 8; byte_index++) {
                payload_length = (payload_length << 8) | extended_length[byte_index];
            }
        }

        // 当前服务端控制消息不允许分片或超出固定上限。
        if (!final_fragment || payload_length > XN_WS_MAX_INCOMING_PAYLOAD) {
            break;
        }

        // 保存服务端可选掩码。
        uint8_t mask_key[4] = {0};

        // 兼容读取错误带掩码的服务器帧。
        if (masked && xn_ws_ssl_read_all(client->ssl, mask_key, sizeof(mask_key)) != 0) {
            break;
        }

        // 为负载和文本结尾空字符分配内存。
        uint8_t *payload = malloc((size_t)payload_length + 1);

        // 内存不足时关闭连接。
        if (payload == NULL) {
            break;
        }

        // 读取当前帧全部负载。
        if (payload_length > 0 && xn_ws_ssl_read_all(client->ssl, payload, (size_t)payload_length) != 0) {
            free(payload);
            break;
        }

        // 对错误带掩码的服务器负载执行解码。
        if (masked) {
            for (size_t payload_index = 0; payload_index < (size_t)payload_length; payload_index++) {
                payload[payload_index] ^= mask_key[payload_index % 4];
            }
        }

        // 补充文本消息结尾空字符。
        payload[payload_length] = '\0';

        // 收到关闭帧时释放负载并结束连接。
        if (opcode == 0x08U) {
            free(payload);
            break;
        }

        // 收到 Ping 时立即原样回复 Pong。
        if (opcode == 0x09U) {
            xn_ws_send_frame(client, 0x0AU, payload, (size_t)payload_length);
        } else if (opcode == 0x01U && client->text_callback != NULL) {
            // 收到文本帧时交给业务层处理鉴权和心跳响应。
            client->text_callback(client->callback_user_data, (const char *)payload, (size_t)payload_length);
        } else if (opcode == 0x02U && client->binary_callback != NULL) {
            // 收到二进制帧时交给业务层处理下行对讲音频。
            client->binary_callback(client->callback_user_data, payload, (size_t)payload_length);
        }

        // 释放当前服务器帧负载。
        free(payload);
    }

    // 标记连接已经断开。
    xn_ws_mark_disconnected(client);

    // 结束接收线程。
    return NULL;
}

// 初始化 WebSocket 客户端结构和线程同步对象。
int xn_ws_client_init(
    xn_ws_client *client,
    xn_ws_text_callback text_callback,
    xn_ws_binary_callback binary_callback,
    void *user_data)
{
    // 清空客户端运行状态。
    memset(client, 0, sizeof(*client));

    // 使用无效套接字作为初始状态。
    client->socket_fd = -1;

    // 保存服务器文本消息回调。
    client->text_callback = text_callback;

    // 保存服务器二进制消息回调。
    client->binary_callback = binary_callback;

    // 保存业务层回调用户数据。
    client->callback_user_data = user_data;

    // 初始化发送互斥锁。
    if (pthread_mutex_init(&client->send_mutex, NULL) != 0) {
        return -1;
    }

    // 初始化状态互斥锁。
    if (pthread_mutex_init(&client->state_mutex, NULL) != 0) {
        pthread_mutex_destroy(&client->send_mutex);
        return -1;
    }

    // 初始化 OpenSSL 全局算法和错误字符串。
    OPENSSL_init_ssl(0, NULL);

    // 返回初始化成功。
    return 0;
}

// 建立并校验一条 WSS 连接。
int xn_ws_client_connect(xn_ws_client *client, const char *server_url, const char *ca_file, char *error_message, size_t error_message_length)
{
    // 确保上一次连接资源已经释放。
    xn_ws_client_close(client);

    // 保存解析后的 WSS 地址。
    xn_ws_url parsed_url = {0};

    // 解析生产环境 WSS 地址。
    if (xn_ws_parse_url(server_url, &parsed_url, error_message, error_message_length) != 0) {
        return -1;
    }

    // 建立底层 TCP 连接。
    client->socket_fd = xn_ws_connect_tcp(&parsed_url, error_message, error_message_length);

    // TCP 连接失败时直接返回。
    if (client->socket_fd < 0) {
        return -1;
    }

    // 创建仅使用现代 TLS 客户端方法的上下文。
    client->ssl_context = SSL_CTX_new(TLS_client_method());

    // TLS 上下文创建失败时释放连接。
    if (client->ssl_context == NULL) {
        snprintf(error_message, error_message_length, "TLS 上下文创建失败");
        xn_ws_client_close(client);
        return -1;
    }

    // 正式环境强制校验服务器证书链。
    SSL_CTX_set_verify(client->ssl_context, SSL_VERIFY_PEER, NULL);

    // 加载随程序部署的可信 CA 证书包。
    if (SSL_CTX_load_verify_locations(client->ssl_context, ca_file, NULL) != 1) {
        snprintf(error_message, error_message_length, "无法加载 CA 证书文件 %s", ca_file);
        xn_ws_client_close(client);
        return -1;
    }

    // 创建当前 TCP 连接对应的 TLS 会话。
    client->ssl = SSL_new(client->ssl_context);

    // TLS 会话创建失败时释放连接。
    if (client->ssl == NULL) {
        snprintf(error_message, error_message_length, "TLS 会话创建失败");
        xn_ws_client_close(client);
        return -1;
    }

    // 设置 SNI 主机名，确保反向代理返回正确证书。
    if (SSL_set_tlsext_host_name(client->ssl, parsed_url.host) != 1) {
        snprintf(error_message, error_message_length, "TLS SNI 设置失败");
        xn_ws_client_close(client);
        return -1;
    }

    // 设置证书主机名校验目标。
    if (SSL_set1_host(client->ssl, parsed_url.host) != 1) {
        snprintf(error_message, error_message_length, "TLS 主机名校验设置失败");
        xn_ws_client_close(client);
        return -1;
    }

    // 将 TLS 会话绑定到底层 TCP 套接字。
    SSL_set_fd(client->ssl, client->socket_fd);

    // 完成 TLS 握手和服务器证书校验。
    if (SSL_connect(client->ssl) != 1) {
        snprintf(error_message, error_message_length, "TLS 握手失败");
        xn_ws_client_close(client);
        return -1;
    }

    // 再次确认服务器证书验证结果。
    if (SSL_get_verify_result(client->ssl) != X509_V_OK) {
        snprintf(error_message, error_message_length, "服务器证书校验失败");
        xn_ws_client_close(client);
        return -1;
    }

    // 完成 WebSocket HTTP Upgrade 握手。
    if (xn_ws_perform_handshake(client, &parsed_url, error_message, error_message_length) != 0) {
        xn_ws_client_close(client);
        return -1;
    }

    // 握手完成后允许接收线程长期等待，连接停止仍由 shutdown 立即唤醒。
    if (xn_ws_clear_socket_receive_timeout(client->socket_fd) != 0) {
        snprintf(error_message, error_message_length, "WebSocket 运行期接收模式设置失败");
        xn_ws_client_close(client);
        return -1;
    }

    // 标记连接和接收线程进入运行状态。
    pthread_mutex_lock(&client->state_mutex);
    client->running = 1;
    client->connected = 1;
    pthread_mutex_unlock(&client->state_mutex);

    // 启动服务器消息接收线程。
    if (pthread_create(&client->receiver_thread, NULL, xn_ws_receiver_main, client) != 0) {
        snprintf(error_message, error_message_length, "WebSocket 接收线程创建失败");
        xn_ws_client_close(client);
        return -1;
    }

    // 返回 WSS 连接成功。
    return 0;
}

// 发送 UTF-8 文本 WebSocket 帧。
int xn_ws_client_send_text(xn_ws_client *client, const char *message, size_t message_length)
{
    // 使用文本操作码发送完整消息。
    return xn_ws_send_frame(client, 0x01U, (const uint8_t *)message, message_length);
}

// 发送二进制 WebSocket 帧。
int xn_ws_client_send_binary(xn_ws_client *client, const uint8_t *payload, size_t payload_length)
{
    // 使用二进制操作码发送完整媒体包。
    return xn_ws_send_frame(client, 0x02U, payload, payload_length);
}

// 查询当前连接是否仍可使用。
int xn_ws_client_is_connected(xn_ws_client *client)
{
    // 加锁读取共享连接状态。
    pthread_mutex_lock(&client->state_mutex);

    // 保存当前连接状态。
    int connected = client->connected;

    // 解锁共享连接状态。
    pthread_mutex_unlock(&client->state_mutex);

    // 返回当前连接状态。
    return connected;
}

// 关闭当前连接并等待接收线程退出。
void xn_ws_client_close(xn_ws_client *client)
{
    // 加锁更新运行状态。
    pthread_mutex_lock(&client->state_mutex);

    // 通知接收线程停止。
    client->running = 0;
    client->connected = 0;

    // 解锁运行状态。
    pthread_mutex_unlock(&client->state_mutex);

    // 关闭套接字读写以唤醒阻塞中的 TLS 接收。
    if (client->socket_fd >= 0) {
        shutdown(client->socket_fd, SHUT_RDWR);
    }

    // 等待已经创建的接收线程退出。
    if (client->receiver_thread != (pthread_t)0) {
        pthread_join(client->receiver_thread, NULL);
        client->receiver_thread = (pthread_t)0;
    }

    // 尝试发送 TLS 关闭通知并释放会话。
    if (client->ssl != NULL) {
        SSL_shutdown(client->ssl);
        SSL_free(client->ssl);
        client->ssl = NULL;
    }

    // 释放 TLS 上下文。
    if (client->ssl_context != NULL) {
        SSL_CTX_free(client->ssl_context);
        client->ssl_context = NULL;
    }

    // 关闭底层 TCP 套接字。
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
    }
}

// 销毁 WebSocket 客户端同步对象。
void xn_ws_client_destroy(xn_ws_client *client)
{
    // 先关闭可能仍存在的网络连接。
    xn_ws_client_close(client);

    // 销毁发送互斥锁。
    pthread_mutex_destroy(&client->send_mutex);

    // 销毁状态互斥锁。
    pthread_mutex_destroy(&client->state_mutex);
}
