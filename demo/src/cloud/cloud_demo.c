// 引入云端参考实现接口。
#include "cloud_demo.h"

// 引入当前生产 WSS 和媒体协议参考模块。
#include "internal/media_packet.h"
#include "internal/time_utils.h"
#include "internal/websocket_client.h"

// 引入线程、内存、文本和时间接口。
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 限制 Demo 应用层媒体队列最多保存六十四包。
#define WORKCARD_CLOUD_QUEUE_MAX_COUNT 64U

// 限制 Demo 应用层媒体队列最多占用四兆字节。
#define WORKCARD_CLOUD_QUEUE_MAX_BYTES (4U * 1024U * 1024U)

// 保存一条等待应用层 WSS 发送的媒体数据。
typedef struct workcard_cloud_media_node {
    // 保存下一队列节点。
    struct workcard_cloud_media_node *next;

    // 保存当前生产媒体协议类型。
    uint8_t media_type;

    // 保存关键帧和不连续标志。
    uint8_t flags;

    // 保存对齐保留字段。
    uint16_t reserved;

    // 保存媒体包序号。
    uint32_t sequence;

    // 保存设备单调时钟微秒时间戳。
    uint64_t timestamp_us;

    // 保存紧随节点分配的媒体数据字节数。
    uint32_t data_size;

    // 使用柔性数组保存回调数据副本。
    uint8_t data[];
} workcard_cloud_media_node_t;

// 保存当前云端参考客户端的全部运行状态。
struct workcard_cloud_demo {
    // 保存底层 SDK 句柄，所有硬件操作均通过公开 API 完成。
    workcard_sdk_t *sdk;

    // 保存不含运行时状态的配置副本。
    xn_app_config config;

    // 保存当前生产 WSS 客户端。
    xn_ws_client websocket;

    // 保存连接、鉴权和心跳线程。
    pthread_t network_thread;

    // 标记网络线程已经创建。
    int network_thread_started;

    // 保护运行、鉴权和上传门控状态。
    pthread_mutex_t state_mutex;

    // 在服务器返回 auth-ok 时唤醒鉴权等待。
    pthread_cond_t auth_condition;

    // 保护应用层媒体发送队列。
    pthread_mutex_t media_queue_mutex;

    // 在新增媒体数据或停止时唤醒应用层发送线程。
    pthread_cond_t media_queue_condition;

    // 保存应用层媒体发送线程。
    pthread_t media_sender_thread;

    // 标记应用层发送线程已经创建。
    int media_sender_thread_started;

    // 标记应用层发送线程是否继续运行。
    int media_sender_running;

    // 保存应用层媒体队列首节点。
    workcard_cloud_media_node_t *media_queue_head;

    // 保存应用层媒体队列尾节点。
    workcard_cloud_media_node_t *media_queue_tail;

    // 保存应用层媒体队列节点数量。
    uint32_t media_queue_count;

    // 保存应用层媒体队列总字节数。
    uint32_t media_queue_bytes;

    // 标记 Demo 是否应继续维护云端连接。
    int running;

    // 标记服务器是否已经完成设备鉴权。
    int authenticated;

    // 标记后台是否允许当前设备上传媒体。
    int media_upload_enabled;

    // 标记下一轮立即重建 WSS 以刷新动态媒体鉴权参数。
    int reconnect_requested;
};

// 前置声明应用层媒体发送线程入口。
static void *workcard_cloud_media_sender_main(void *argument);

// 原子更新鉴权状态并唤醒等待线程。
static void workcard_cloud_set_authenticated(workcard_cloud_demo_t *cloud, int authenticated)
{
    // 使用状态锁保护共享字段。
    pthread_mutex_lock(&cloud->state_mutex);
    cloud->authenticated = authenticated;
    pthread_cond_broadcast(&cloud->auth_condition);
    pthread_mutex_unlock(&cloud->state_mutex);
}

// 原子更新媒体上传门控。
static void workcard_cloud_set_upload_enabled(workcard_cloud_demo_t *cloud, int enabled)
{
    // 使用状态锁保护共享字段。
    pthread_mutex_lock(&cloud->state_mutex);
    cloud->media_upload_enabled = enabled;
    pthread_mutex_unlock(&cloud->state_mutex);
}

// 查询 Demo 是否仍应维护云端连接。
static int workcard_cloud_is_running(workcard_cloud_demo_t *cloud)
{
    // 在锁保护下读取运行状态。
    pthread_mutex_lock(&cloud->state_mutex);
    const int running = cloud->running;
    pthread_mutex_unlock(&cloud->state_mutex);
    return running;
}

// 查询是否收到动态配置触发的重连请求。
static int workcard_cloud_reconnect_requested(workcard_cloud_demo_t *cloud)
{
    // 在状态锁下读取重连标志。
    pthread_mutex_lock(&cloud->state_mutex);
    const int requested = cloud->reconnect_requested;
    pthread_mutex_unlock(&cloud->state_mutex);
    return requested;
}

// 查询当前媒体包是否允许上传。
static int workcard_cloud_can_upload(workcard_cloud_demo_t *cloud)
{
    // 在锁保护下读取鉴权和上传门控。
    pthread_mutex_lock(&cloud->state_mutex);
    const int allowed = cloud->running && cloud->authenticated && cloud->media_upload_enabled;
    pthread_mutex_unlock(&cloud->state_mutex);
    return allowed;
}

// 处理当前云端下发的控制文本。
static void workcard_cloud_handle_text(void *user_data, const char *message, size_t message_length)
{
    // 恢复云端参考客户端状态。
    workcard_cloud_demo_t *cloud = user_data;

    // WebSocket 模块已经保证文本结尾存在空字符。
    (void)message_length;

    // 鉴权成功后等待后台按需打开媒体上传。
    if (strstr(message, "\"type\":\"auth-ok\"") != NULL) {
        workcard_cloud_set_authenticated(cloud, 1);
        workcard_cloud_set_upload_enabled(cloud, 0);
        fprintf(stdout, "云端鉴权成功，等待实时查看指令\n");
        return;
    }

    // 后台开始查看时请求新的 H.264 IDR 并打开上传门控。
    if (strstr(message, "\"type\":\"media-start\"") != NULL) {
        (void)workcard_camera_request_keyframe(cloud->sdk);
        workcard_cloud_set_upload_enabled(cloud, 1);
        fprintf(stdout, "后台实时查看已经打开\n");
        return;
    }

    // 后台停止查看时立即关闭上传门控。
    if (strstr(message, "\"type\":\"media-stop\"") != NULL) {
        workcard_cloud_set_upload_enabled(cloud, 0);
        fprintf(stdout, "后台实时查看已经关闭\n");
        return;
    }

    // 后台开始对讲时打开 SDK 下行 Opus 播放并返回 ready。
    if (strstr(message, "\"type\":\"intercom-start\"") != NULL) {
        if (workcard_audio_set_playback_enabled(cloud->sdk, 1U) == WORKCARD_OK) {
            const char ready_message[] = "{\"type\":\"intercom-ready\"}";
            (void)xn_ws_client_send_text(
                &cloud->websocket,
                ready_message,
                sizeof(ready_message) - 1U);
        }
        return;
    }

    // 后台停止对讲时关闭播放门控并清理旧播放数据。
    if (strstr(message, "\"type\":\"intercom-stop\"") != NULL) {
        (void)workcard_audio_set_playback_enabled(cloud->sdk, 0U);
        return;
    }

    // 业务错误只记录服务器返回文本，不输出本地设备密钥。
    if (strstr(message, "\"type\":\"error\"") != NULL) {
        fprintf(stderr, "云端返回业务错误: %s\n", message);
    }
}

// 处理当前云端下发的对讲二进制包。
static void workcard_cloud_handle_binary(
    void *user_data,
    const uint8_t *payload,
    size_t payload_length)
{
    // 恢复云端参考客户端状态。
    workcard_cloud_demo_t *cloud = user_data;

    // 解析当前生产环境使用的二十四字节媒体头。
    xn_media_packet_view packet;
    memset(&packet, 0, sizeof(packet));
    if (xn_media_packet_parse(payload, payload_length, &packet) != 0 ||
        packet.media_type != XN_MEDIA_TYPE_INTERCOM_AUDIO) {
        return;
    }

    // 将媒体协议视图转换为公开 SDK 音频帧。
    workcard_audio_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.codec = WORKCARD_AUDIO_CODEC_OPUS;
    frame.flags = packet.flags;
    frame.sequence = packet.sequence;
    frame.timestamp_us = packet.timestamp_us;
    frame.data = packet.payload;
    frame.data_size = packet.payload_length;

    // 通过公开 SDK 提交给内置扬声器或蓝牙 A2DP 输出。
    const workcard_result_t result = workcard_audio_submit_playback(cloud->sdk, &frame);
    if (result != WORKCARD_OK) {
        fprintf(stderr, "下行对讲播放失败: %s\n", workcard_result_string(result));
    }
}

// 构建当前云端要求的设备鉴权 JSON。
static int workcard_cloud_build_auth(
    const xn_app_config *config,
    char *message,
    size_t message_size)
{
    // 使用与现有生产后台一致的媒体能力格式。
    const int length = snprintf(
        message,
        message_size,
        "{\"type\":\"auth\",\"deviceId\":\"%s\",\"deviceSecret\":\"%s\","
        "\"firmwareVersion\":\"%s\","
        "\"video\":{\"codec\":\"avc1.42E01E\",\"width\":%u,\"height\":%u,\"fps\":%u},"
        "\"audio\":{\"codec\":\"opus\",\"sampleRate\":%u,\"channels\":%u}}",
        config->device_id,
        config->device_secret,
        config->firmware_version,
        config->video_width,
        config->video_height,
        config->video_fps,
        config->audio_sample_rate,
        config->audio_channels);

    // 拒绝生成失败或被截断的鉴权消息。
    return length > 0 && (size_t)length < message_size ? length : -1;
}

// 等待服务器在六秒内完成设备鉴权。
static int workcard_cloud_wait_for_auth(workcard_cloud_demo_t *cloud)
{
    // 构造六秒后的绝对超时时间。
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 6;

    // 等待鉴权成功、连接关闭或超时。
    pthread_mutex_lock(&cloud->state_mutex);
    int wait_result = 0;
    while (cloud->running && !cloud->authenticated &&
           xn_ws_client_is_connected(&cloud->websocket) && wait_result == 0) {
        wait_result = pthread_cond_timedwait(&cloud->auth_condition, &cloud->state_mutex, &deadline);
    }
    const int authenticated = cloud->authenticated;
    pthread_mutex_unlock(&cloud->state_mutex);
    return authenticated;
}

// 运行当前生产 WSS 的连接、鉴权、心跳和重连循环。
static void *workcard_cloud_network_main(void *argument)
{
    // 恢复云端参考客户端状态。
    workcard_cloud_demo_t *cloud = argument;

    // 从一秒开始执行有上限的指数退避。
    unsigned int reconnect_delay = 1U;
    while (workcard_cloud_is_running(cloud)) {
        // 新连接默认关闭鉴权、上传和对讲状态。
        workcard_cloud_set_authenticated(cloud, 0);
        workcard_cloud_set_upload_enabled(cloud, 0);
        (void)workcard_audio_set_playback_enabled(cloud->sdk, 0U);

        // 建立并校验当前生产 WSS 连接。
        char error_message[256];
        memset(error_message, 0, sizeof(error_message));
        if (xn_ws_client_connect(
                &cloud->websocket,
                cloud->config.server_url,
                cloud->config.ca_file,
                error_message,
                sizeof(error_message)) != 0) {
            fprintf(stderr, "云端连接失败: %s，%u 秒后重试\n", error_message, reconnect_delay);
            sleep(reconnect_delay);
            reconnect_delay = reconnect_delay < 4U ? reconnect_delay * 2U : 4U;
            continue;
        }

        // 在状态锁下复制动态视频参数，避免 MQTT 配置线程并发修改鉴权结构。
        xn_app_config auth_config;
        pthread_mutex_lock(&cloud->state_mutex);
        auth_config = cloud->config;
        pthread_mutex_unlock(&cloud->state_mutex);

        // 构建并发送不会写入日志的设备鉴权消息。
        char auth_message[2048];
        const int auth_length = workcard_cloud_build_auth(
            &auth_config,
            auth_message,
            sizeof(auth_message));
        if (auth_length <= 0 ||
            xn_ws_client_send_text(&cloud->websocket, auth_message, (size_t)auth_length) != 0 ||
            !workcard_cloud_wait_for_auth(cloud)) {
            xn_ws_client_close(&cloud->websocket);
            sleep(reconnect_delay);
            reconnect_delay = reconnect_delay < 4U ? reconnect_delay * 2U : 4U;
            continue;
        }

        // 鉴权成功后恢复初始重连等待并请求后续首个 IDR。
        reconnect_delay = 1U;
        (void)workcard_camera_request_keyframe(cloud->sdk);
        uint64_t last_heartbeat_ms = xn_unix_time_ms();

        // 当前连接存活期间每十五秒发送一次应用层心跳。
        while (workcard_cloud_is_running(cloud) && !workcard_cloud_reconnect_requested(cloud) &&
               xn_ws_client_is_connected(&cloud->websocket)) {
            const uint64_t current_time_ms = xn_unix_time_ms();
            if (current_time_ms - last_heartbeat_ms >= 15000ULL) {
                char heartbeat[128];
                const int heartbeat_length = snprintf(
                    heartbeat,
                    sizeof(heartbeat),
                    "{\"type\":\"heartbeat\",\"deviceTime\":%llu}",
                    (unsigned long long)current_time_ms);
                if (heartbeat_length <= 0 ||
                    xn_ws_client_send_text(&cloud->websocket, heartbeat, (size_t)heartbeat_length) != 0) {
                    break;
                }
                last_heartbeat_ms = current_time_ms;
            }
            sleep(1U);
        }

        // 断开后关闭所有云端门控并清理当前 TLS 会话。
        workcard_cloud_set_authenticated(cloud, 0);
        workcard_cloud_set_upload_enabled(cloud, 0);
        (void)workcard_audio_set_playback_enabled(cloud->sdk, 0U);
        xn_ws_client_close(&cloud->websocket);

        // 主动停止时不再执行退避。
        pthread_mutex_lock(&cloud->state_mutex);
        const int reconnect_now = cloud->reconnect_requested;
        cloud->reconnect_requested = 0;
        pthread_mutex_unlock(&cloud->state_mutex);
        if (workcard_cloud_is_running(cloud) && !reconnect_now) {
            sleep(reconnect_delay);
            reconnect_delay = reconnect_delay < 4U ? reconnect_delay * 2U : 4U;
        }
    }

    // 结束云端网络线程。
    return NULL;
}

// 更新下一次 WSS 鉴权使用的动态视频参数。
void workcard_cloud_demo_update_video_config(
    workcard_cloud_demo_t *cloud,
    const workcard_camera_config_t *config)
{
    // 空参数不修改云端客户端状态。
    if (cloud == NULL || config == NULL) {
        return;
    }
    pthread_mutex_lock(&cloud->state_mutex);
    cloud->config.video_width = config->width;
    cloud->config.video_height = config->height;
    cloud->config.video_fps = config->fps;
    cloud->config.video_gop = config->gop;
    cloud->config.video_bitrate_bps = config->bitrate_bps;
    cloud->reconnect_requested = 1;
    pthread_mutex_unlock(&cloud->state_mutex);
}

// 创建使用当前生产协议的云端参考客户端。
int workcard_cloud_demo_create(
    workcard_sdk_t *sdk,
    const xn_app_config *config,
    workcard_cloud_demo_t **cloud_pointer)
{
    // 调用参数必须完整有效。
    if (sdk == NULL || config == NULL || cloud_pointer == NULL) {
        return -1;
    }

    // 分配清零后的云端状态。
    workcard_cloud_demo_t *cloud = calloc(1U, sizeof(*cloud));
    if (cloud == NULL) {
        return -1;
    }

    // 保存 SDK 句柄和配置副本。
    cloud->sdk = sdk;
    cloud->config = *config;

    // 初始化状态互斥锁。
    if (pthread_mutex_init(&cloud->state_mutex, NULL) != 0) {
        free(cloud);
        return -1;
    }

    // 初始化鉴权条件变量。
    if (pthread_cond_init(&cloud->auth_condition, NULL) != 0) {
        pthread_mutex_destroy(&cloud->state_mutex);
        free(cloud);
        return -1;
    }

    // 初始化应用层媒体队列互斥锁。
    if (pthread_mutex_init(&cloud->media_queue_mutex, NULL) != 0) {
        pthread_cond_destroy(&cloud->auth_condition);
        pthread_mutex_destroy(&cloud->state_mutex);
        free(cloud);
        return -1;
    }

    // 初始化应用层媒体队列条件变量。
    if (pthread_cond_init(&cloud->media_queue_condition, NULL) != 0) {
        pthread_mutex_destroy(&cloud->media_queue_mutex);
        pthread_cond_destroy(&cloud->auth_condition);
        pthread_mutex_destroy(&cloud->state_mutex);
        free(cloud);
        return -1;
    }

    // 初始化当前生产 WebSocket 客户端。
    if (xn_ws_client_init(
            &cloud->websocket,
            workcard_cloud_handle_text,
            workcard_cloud_handle_binary,
            cloud) != 0) {
        pthread_cond_destroy(&cloud->media_queue_condition);
        pthread_mutex_destroy(&cloud->media_queue_mutex);
        pthread_cond_destroy(&cloud->auth_condition);
        pthread_mutex_destroy(&cloud->state_mutex);
        free(cloud);
        return -1;
    }

    // 返回创建完成的云端参考客户端。
    *cloud_pointer = cloud;
    return 0;
}

// 启动 WSS 连接、鉴权、心跳和重连线程。
int workcard_cloud_demo_start(workcard_cloud_demo_t *cloud)
{
    // 空句柄或已启动状态不能重复创建线程。
    if (cloud == NULL || cloud->network_thread_started) {
        return -1;
    }

    // 发布运行状态。
    pthread_mutex_lock(&cloud->state_mutex);
    cloud->running = 1;
    pthread_mutex_unlock(&cloud->state_mutex);

    // 启动独立应用层媒体发送线程。
    pthread_mutex_lock(&cloud->media_queue_mutex);
    cloud->media_sender_running = 1;
    pthread_mutex_unlock(&cloud->media_queue_mutex);
    if (pthread_create(&cloud->media_sender_thread, NULL, workcard_cloud_media_sender_main, cloud) != 0) {
        pthread_mutex_lock(&cloud->state_mutex);
        cloud->running = 0;
        pthread_mutex_unlock(&cloud->state_mutex);
        pthread_mutex_lock(&cloud->media_queue_mutex);
        cloud->media_sender_running = 0;
        pthread_mutex_unlock(&cloud->media_queue_mutex);
        return -1;
    }
    cloud->media_sender_thread_started = 1;

    // 创建网络连接、鉴权和心跳线程。
    if (pthread_create(&cloud->network_thread, NULL, workcard_cloud_network_main, cloud) != 0) {
        pthread_mutex_lock(&cloud->state_mutex);
        cloud->running = 0;
        pthread_mutex_unlock(&cloud->state_mutex);
        pthread_mutex_lock(&cloud->media_queue_mutex);
        cloud->media_sender_running = 0;
        pthread_cond_broadcast(&cloud->media_queue_condition);
        pthread_mutex_unlock(&cloud->media_queue_mutex);
        pthread_join(cloud->media_sender_thread, NULL);
        cloud->media_sender_thread_started = 0;
        return -1;
    }

    // 标记停止阶段需要回收网络线程。
    cloud->network_thread_started = 1;
    return 0;
}

// 发送一包 H.264 或 Opus 媒体数据。
static void workcard_cloud_send_media(
    workcard_cloud_demo_t *cloud,
    uint8_t media_type,
    uint8_t flags,
    uint32_t sequence,
    uint64_t timestamp_us,
    const uint8_t *data,
    size_t data_size)
{
    // 未鉴权、未获得观看授权或连接断开时不上传。
    if (cloud == NULL || data == NULL || data_size == 0U ||
        !workcard_cloud_can_upload(cloud) || !xn_ws_client_is_connected(&cloud->websocket)) {
        return;
    }

    // 构建当前生产环境使用的二十四字节媒体头和负载。
    uint8_t *packet = NULL;
    size_t packet_size = 0U;
    if (xn_media_packet_build(
            media_type,
            flags,
            sequence,
            timestamp_us,
            data,
            data_size,
            &packet,
            &packet_size) != 0) {
        return;
    }

    // 发送完整 WSS 二进制帧并释放临时封包。
    (void)xn_ws_client_send_binary(&cloud->websocket, packet, packet_size);
    free(packet);
}

// 删除应用层媒体队列首节点并更新统计。
static void workcard_cloud_drop_media_head(workcard_cloud_demo_t *cloud)
{
    // 空队列无需操作。
    if (cloud->media_queue_head == NULL) {
        return;
    }

    // 摘除并保存旧首节点。
    workcard_cloud_media_node_t *node = cloud->media_queue_head;
    cloud->media_queue_head = node->next;
    if (cloud->media_queue_head == NULL) {
        cloud->media_queue_tail = NULL;
    }

    // 更新节点和字节统计。
    cloud->media_queue_count--;
    cloud->media_queue_bytes -= node->data_size;

    // 释放旧媒体副本。
    free(node);
}

// 将 SDK 回调数据快速复制到应用层有界队列。
static void workcard_cloud_enqueue_media(
    workcard_cloud_demo_t *cloud,
    uint8_t media_type,
    uint8_t flags,
    uint32_t sequence,
    uint64_t timestamp_us,
    const uint8_t *data,
    size_t data_size)
{
    // 拒绝空数据和超过正式队列上限的单包。
    if (cloud == NULL || data == NULL || data_size == 0U || data_size > WORKCARD_CLOUD_QUEUE_MAX_BYTES) {
        return;
    }

    // 分配连续节点和媒体数据副本。
    workcard_cloud_media_node_t *node = malloc(sizeof(*node) + data_size);
    if (node == NULL) {
        return;
    }

    // 填写媒体元数据和回调数据副本。
    memset(node, 0, sizeof(*node));
    node->media_type = media_type;
    node->flags = flags;
    node->sequence = sequence;
    node->timestamp_us = timestamp_us;
    node->data_size = (uint32_t)data_size;
    memcpy(node->data, data, data_size);

    // 在锁保护下将节点加入有界队列。
    pthread_mutex_lock(&cloud->media_queue_mutex);
    if (!cloud->media_sender_running) {
        pthread_mutex_unlock(&cloud->media_queue_mutex);
        free(node);
        return;
    }

    // 队列达到数量或字节上限时丢弃最旧数据。
    while (cloud->media_queue_head != NULL &&
           (cloud->media_queue_count >= WORKCARD_CLOUD_QUEUE_MAX_COUNT ||
            cloud->media_queue_bytes + node->data_size > WORKCARD_CLOUD_QUEUE_MAX_BYTES)) {
        workcard_cloud_drop_media_head(cloud);
    }

    // 将新节点追加到队尾。
    if (cloud->media_queue_tail == NULL) {
        cloud->media_queue_head = node;
        cloud->media_queue_tail = node;
    } else {
        cloud->media_queue_tail->next = node;
        cloud->media_queue_tail = node;
    }
    cloud->media_queue_count++;
    cloud->media_queue_bytes += node->data_size;
    pthread_cond_signal(&cloud->media_queue_condition);
    pthread_mutex_unlock(&cloud->media_queue_mutex);
}

// 持续从应用层队列发送当前云端媒体数据。
static void *workcard_cloud_media_sender_main(void *argument)
{
    // 恢复云端参考客户端状态。
    workcard_cloud_demo_t *cloud = argument;

    // 持续等待媒体节点或停止信号。
    for (;;) {
        // 在队列锁下等待新媒体。
        pthread_mutex_lock(&cloud->media_queue_mutex);
        while (cloud->media_sender_running && cloud->media_queue_head == NULL) {
            pthread_cond_wait(&cloud->media_queue_condition, &cloud->media_queue_mutex);
        }

        // 停止时丢弃残留媒体并结束线程。
        if (!cloud->media_sender_running) {
            while (cloud->media_queue_head != NULL) {
                workcard_cloud_drop_media_head(cloud);
            }
            pthread_mutex_unlock(&cloud->media_queue_mutex);
            break;
        }

        // 摘除首节点并在锁外执行网络发送。
        workcard_cloud_media_node_t *node = cloud->media_queue_head;
        cloud->media_queue_head = node->next;
        if (cloud->media_queue_head == NULL) {
            cloud->media_queue_tail = NULL;
        }
        cloud->media_queue_count--;
        cloud->media_queue_bytes -= node->data_size;
        pthread_mutex_unlock(&cloud->media_queue_mutex);

        // 由独立应用层线程完成 WSS 封包和发送。
        workcard_cloud_send_media(
            cloud,
            node->media_type,
            node->flags,
            node->sequence,
            node->timestamp_us,
            node->data,
            node->data_size);
        free(node);
    }

    // 结束应用层媒体发送线程。
    return NULL;
}

// 将 SDK 回调的 H.264 帧按当前媒体协议发送到云端。
void workcard_cloud_demo_submit_video(
    workcard_cloud_demo_t *cloud,
    const workcard_video_frame_t *frame)
{
    // 空帧不能进入云端封包。
    if (frame == NULL) {
        return;
    }

    // 公开视频标志与当前媒体协议使用相同位定义。
    workcard_cloud_enqueue_media(
        cloud,
        XN_MEDIA_TYPE_VIDEO,
        (uint8_t)frame->flags,
        frame->sequence,
        frame->timestamp_us,
        frame->data,
        frame->data_size);
}

// 将 SDK 回调的 Opus 包按当前媒体协议发送到云端。
void workcard_cloud_demo_submit_audio(
    workcard_cloud_demo_t *cloud,
    const workcard_audio_frame_t *frame)
{
    // 空帧不能进入云端封包。
    if (frame == NULL) {
        return;
    }

    // 公开视频标志与当前媒体协议使用相同位定义。
    workcard_cloud_enqueue_media(
        cloud,
        XN_MEDIA_TYPE_AUDIO,
        (uint8_t)frame->flags,
        frame->sequence,
        frame->timestamp_us,
        frame->data,
        frame->data_size);
}

// 停止网络线程并关闭当前 WSS 连接。
void workcard_cloud_demo_stop(workcard_cloud_demo_t *cloud)
{
    // 空句柄允许安全清理。
    if (cloud == NULL) {
        return;
    }

    // 发布停止状态并唤醒鉴权等待。
    pthread_mutex_lock(&cloud->state_mutex);
    cloud->running = 0;
    cloud->authenticated = 0;
    cloud->media_upload_enabled = 0;
    pthread_cond_broadcast(&cloud->auth_condition);
    pthread_mutex_unlock(&cloud->state_mutex);

    // 关闭 TLS 会话以唤醒 WebSocket 接收和网络线程。
    xn_ws_client_close(&cloud->websocket);

    // 回收已经创建的网络线程。
    if (cloud->network_thread_started) {
        pthread_join(cloud->network_thread, NULL);
        cloud->network_thread_started = 0;
    }

    // 停止并回收应用层媒体发送线程。
    pthread_mutex_lock(&cloud->media_queue_mutex);
    cloud->media_sender_running = 0;
    pthread_cond_broadcast(&cloud->media_queue_condition);
    pthread_mutex_unlock(&cloud->media_queue_mutex);
    if (cloud->media_sender_thread_started) {
        pthread_join(cloud->media_sender_thread, NULL);
        cloud->media_sender_thread_started = 0;
    }
}

// 销毁云端参考客户端私有状态。
void workcard_cloud_demo_destroy(workcard_cloud_demo_t *cloud)
{
    // 空句柄允许安全清理。
    if (cloud == NULL) {
        return;
    }

    // 确保网络线程和 TLS 会话已经停止。
    workcard_cloud_demo_stop(cloud);
    xn_ws_client_destroy(&cloud->websocket);

    // 销毁同步对象并释放状态。
    pthread_cond_destroy(&cloud->media_queue_condition);
    pthread_mutex_destroy(&cloud->media_queue_mutex);
    pthread_cond_destroy(&cloud->auth_condition);
    pthread_mutex_destroy(&cloud->state_mutex);
    free(cloud);
}
