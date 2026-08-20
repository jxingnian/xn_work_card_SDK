#define MQTTCLIENT_PLATFORM_HEADER mqtt_tls_platform.h

// Paho 头文件直接定义该函数，在 Demo 编译单元中重命名以避免与 MQTTClient.c 重复导出。
#define MQTTIsConnected workcard_mqtt_header_is_connected

// 引入固定版本 Paho Embedded C 高级客户端。
#include "MQTTClient.h"

// 撤销只用于头文件兼容的函数重命名。
#undef MQTTIsConnected

// 引入 MQTT Demo 公开生命周期接口。
#include "mqtt_demo.h"

// 引入 Demo 私有 MQTT TLS 平台适配。
#include "internal/mqtt_tls_platform.h"

// 引入后台 Desired 摄像头配置解析器。
#include "internal/desired_config_parser.h"

// 引入 WiFi 命令解析和有线保活切网模块。
#include "internal/wifi_command_parser.h"
#include "network/network_failover.h"
#include "network/wifi_profile_store.h"
#include "network/wifi_text_codec.h"

// 引入线程、时间、文本和内存接口。
#include <pthread.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// 固定 MQTT 发送和接收缓冲区，限制嵌入式内存占用。
#define WORKCARD_MQTT_BUFFER_SIZE (8U * 1024U)

// 固定状态上报周期为六十秒。
#define WORKCARD_MQTT_STATE_INTERVAL_SECONDS 60U

// 定义待处理 WiFi MQTT 命令类型。
typedef enum {
    WORKCARD_WIFI_PENDING_NONE = 0,
    WORKCARD_WIFI_PENDING_PROFILES = 1,
    WORKCARD_WIFI_PENDING_SCAN = 2,
    WORKCARD_WIFI_PENDING_ACTION = 3
} workcard_wifi_pending_type_t;

// 保存应用层 MQTT 参考客户端的全部运行状态。
struct workcard_mqtt_demo {
    // 保存当前 Demo 会话使用的 SDK 句柄。
    workcard_sdk_t *sdk;

    // 保存不含指针的 Demo 配置副本。
    xn_app_config config;

    // 保存 MQTT 网络工作线程。
    pthread_t thread;

    // 保存线程是否已经创建。
    int thread_started;

    // 保存工作线程是否应继续运行。
    int running;

    // 保存工作线程和停止流程共享状态的互斥锁。
    pthread_mutex_t mutex;

    // 保存应用层摄像头重配置回调。
    workcard_mqtt_camera_apply_callback camera_apply_callback;

    // 保存摄像头重配置回调上下文。
    void *camera_apply_user_data;

    // 保存当前已经确认生效的配置 revision。
    unsigned int configuration_revision;

    // 保存 MQTT 回调收到的待处理 Desired 文本。
    char desired_payload[WORKCARD_MQTT_BUFFER_SIZE];

    // 保存待处理 Desired 文本长度。
    size_t desired_payload_length;

    // 标记是否存在待处理 Desired。
    int desired_pending;

    // 保存 MQTT 回调收到的待处理 WiFi 命令文本。
    char wifi_payload[WORKCARD_MQTT_BUFFER_SIZE];

    // 保存待处理 WiFi 命令长度。
    size_t wifi_payload_length;

    // 保存待处理 WiFi 命令类型。
    workcard_wifi_pending_type_t wifi_pending_type;

    // 保存首次失败切网后的原始路由，成功连接后再恢复 WiFi。
    workcard_network_failover_t network_failover;

    // 保存因网络切换未能确认发布的一条 WiFi 动作 Topic。
    char deferred_wifi_topic[448];

    // 保存不含密码且等待新 MQTT 会话补发的 WiFi 动作结果。
    char deferred_wifi_payload[1024];

    // 标记是否存在等待补发的 WiFi 动作结果。
    int deferred_wifi_pending;
};

// 保存当前 MQTT 客户端回调所属的 Demo 对象。
static workcard_mqtt_demo_t *workcard_mqtt_callback_demo = NULL;

// 前置声明 MQTT 发布函数，供配置结果处理函数使用。
static int workcard_mqtt_publish(
    MQTTClient *client,
    const char *topic,
    const char *payload,
    enum QoS qos,
    unsigned char retained);

// 返回锁保护下的 MQTT 运行标志。
static int workcard_mqtt_is_running(workcard_mqtt_demo_t *demo)
{
    // 保存当前状态副本。
    int running = 0;

    // 在互斥锁下读取线程状态。
    pthread_mutex_lock(&demo->mutex);
    running = demo->running;
    pthread_mutex_unlock(&demo->mutex);

    // 返回稳定的状态副本。
    return running;
}

// 保存 MQTT 回调收到的 Desired 配置，避免在 Paho 回调内执行媒体操作。
static void workcard_mqtt_message_arrived(MessageData *message_data)
{
    // 读取当前 MQTT Demo 上下文。
    workcard_mqtt_demo_t *demo = workcard_mqtt_callback_demo;
    if (demo == NULL || message_data == NULL || message_data->message == NULL || message_data->topicName == NULL) {
        return;
    }

    // 构造当前设备允许接收的四类 Topic。
    const char *topic = message_data->topicName->lenstring.data;
    const int topic_length = message_data->topicName->lenstring.len;
    char desired_topic[448];
    char profiles_topic[448];
    char scan_topic[448];
    char action_topic[448];
    (void)snprintf(desired_topic, sizeof(desired_topic), "v1/devices/%s/config/desired", demo->config.device_id);
    (void)snprintf(profiles_topic, sizeof(profiles_topic), "v1/devices/%s/wifi/profiles/set", demo->config.device_id);
    (void)snprintf(scan_topic, sizeof(scan_topic), "v1/devices/%s/wifi/scan/command", demo->config.device_id);
    (void)snprintf(action_topic, sizeof(action_topic), "v1/devices/%s/wifi/action/command", demo->config.device_id);

    // 截断超大消息并保留明确错误，避免覆盖堆栈或静默接受半条配置。
    const size_t payload_length = (size_t)message_data->message->payloadlen;
    pthread_mutex_lock(&demo->mutex);
    if (payload_length < WORKCARD_MQTT_BUFFER_SIZE && topic_length == (int)strlen(desired_topic) && strncmp(topic, desired_topic, (size_t)topic_length) == 0) {
        memcpy(demo->desired_payload, message_data->message->payload, payload_length);
        demo->desired_payload[payload_length] = '\0';
        demo->desired_payload_length = payload_length;
        demo->desired_pending = 1;
    } else if (payload_length < WORKCARD_MQTT_BUFFER_SIZE) {
        workcard_wifi_pending_type_t type = WORKCARD_WIFI_PENDING_NONE;
        if (topic_length == (int)strlen(profiles_topic) && strncmp(topic, profiles_topic, (size_t)topic_length) == 0) type = WORKCARD_WIFI_PENDING_PROFILES;
        else if (topic_length == (int)strlen(scan_topic) && strncmp(topic, scan_topic, (size_t)topic_length) == 0) type = WORKCARD_WIFI_PENDING_SCAN;
        else if (topic_length == (int)strlen(action_topic) && strncmp(topic, action_topic, (size_t)topic_length) == 0) type = WORKCARD_WIFI_PENDING_ACTION;
        if (type != WORKCARD_WIFI_PENDING_NONE) {
            memcpy(demo->wifi_payload, message_data->message->payload, payload_length);
            demo->wifi_payload[payload_length] = '\0';
            demo->wifi_payload_length = payload_length;
            demo->wifi_pending_type = type;
        }
    }
    pthread_mutex_unlock(&demo->mutex);
}

// 发布一次配置处理结果。
static void workcard_mqtt_publish_config_result(
    MQTTClient *client,
    const char *topic_prefix,
    const workcard_desired_config_t *desired,
    unsigned int code,
    const char *message,
    const char *status,
    int duplicate)
{
    // 构造非 Retained 配置结果 Topic。
    char topic[448];
    char payload[1536];
    (void)snprintf(topic, sizeof(topic), "%s/config/result", topic_prefix);
    (void)snprintf(
        payload,
        sizeof(payload),
        "{\"schemaVersion\":1,\"requestId\":\"%s\",\"revision\":%u,\"code\":%u,\"message\":\"%s\",\"data\":{\"status\":\"%s\",\"duplicate\":%s}}",
        desired->request_id,
        desired->revision,
        code,
        message,
        status,
        duplicate ? "true" : "false");
    (void)workcard_mqtt_publish(client, topic, payload, QOS1, 0U);
}

// 发布当前已确认生效的摄像头配置快照。
static void workcard_mqtt_publish_config_reported(
    MQTTClient *client,
    const char *topic_prefix,
    const workcard_mqtt_demo_t *demo,
    const char *apply_status)
{
    // 构造 Retained Reported 配置 Topic。
    char topic[448];
    char payload[1024];
    (void)snprintf(topic, sizeof(topic), "%s/config/reported", topic_prefix);
    (void)snprintf(
        payload,
        sizeof(payload),
        "{\"schemaVersion\":1,\"revision\":%u,\"applyStatus\":\"%s\",\"config\":{\"project\":{\"projectId\":\"\"},\"media\":{\"video\":{\"width\":%u,\"height\":%u,\"fps\":%u,\"gop\":%u,\"bitrateBps\":%u},\"audio\":{\"sampleRate\":%u,\"channels\":%u,\"bitrateBps\":64000,\"captureVolume\":30,\"playbackVolume\":3}}}}",
        demo->configuration_revision,
        apply_status,
        demo->config.video_width,
        demo->config.video_height,
        demo->config.video_fps,
        demo->config.video_gop,
        demo->config.video_bitrate_bps,
        demo->config.audio_sample_rate,
        demo->config.audio_channels);
    (void)workcard_mqtt_publish(client, topic, payload, QOS1, 1U);
}

// 将文本转义为 JSON 字符串内容。
static int workcard_mqtt_json_escape(const char *source, char *target, size_t capacity)
{
    // 逐字节处理 MQTT 状态中的公开文本字段。
    size_t written = 0U;
    for (size_t index = 0U; source[index] != '\0'; ++index) {
        const unsigned char character = (unsigned char)source[index];
        if (character < 0x20U) return -1;
        if (character == '"' || character == '\\') {
            if (written + 2U >= capacity) return -1;
            target[written++] = '\\';
        } else if (written + 1U >= capacity) return -1;
        target[written++] = (char)character;
    }
    target[written] = '\0';
    return 0;
}

// 发布 WiFi 扫描或连接动作的统一结果。
static void workcard_mqtt_publish_wifi_result(MQTTClient *client, const char *topic_prefix, const char *suffix, const char *request_id, unsigned int code, const char *message, const char *status)
{
    // 构造一次性结果 Topic 和不含密码的结果正文。
    char topic[448];
    char payload[768];
    char escaped_request_id[192];
    char escaped_message[256];
    char escaped_status[64];
    if (workcard_mqtt_json_escape(request_id, escaped_request_id, sizeof(escaped_request_id)) != 0 ||
        workcard_mqtt_json_escape(message, escaped_message, sizeof(escaped_message)) != 0 ||
        workcard_mqtt_json_escape(status, escaped_status, sizeof(escaped_status)) != 0) return;
    (void)snprintf(topic, sizeof(topic), "%s/%s", topic_prefix, suffix);
    (void)snprintf(payload, sizeof(payload), "{\"schemaVersion\":1,\"requestId\":\"%s\",\"code\":%u,\"message\":\"%s\",\"status\":\"%s\"}", escaped_request_id, code, escaped_message, escaped_status);
    (void)workcard_mqtt_publish(client, topic, payload, QOS1, 0U);
}

// 发布 WiFi 连接结果，当前会话失败时保存到下一个会话补发。
static void workcard_mqtt_publish_wifi_action_reliable(workcard_mqtt_demo_t *demo, MQTTClient *client, const char *topic_prefix, const char *request_id, unsigned int code, const char *message, const char *status)
{
    // 构造不含密码的动作结果。
    char topic[448];
    char payload[1024];
    char escaped_request_id[192];
    char escaped_message[256];
    char escaped_status[64];
    if (workcard_mqtt_json_escape(request_id, escaped_request_id, sizeof(escaped_request_id)) != 0 ||
        workcard_mqtt_json_escape(message, escaped_message, sizeof(escaped_message)) != 0 ||
        workcard_mqtt_json_escape(status, escaped_status, sizeof(escaped_status)) != 0) return;
    const int topic_length = snprintf(topic, sizeof(topic), "%s/wifi/action/reported", topic_prefix);
    const int payload_length = snprintf(payload, sizeof(payload), "{\"schemaVersion\":1,\"requestId\":\"%s\",\"code\":%u,\"message\":\"%s\",\"status\":\"%s\"}", escaped_request_id, code, escaped_message, escaped_status);
    if (topic_length <= 0 || (size_t)topic_length >= sizeof(topic) || payload_length <= 0 || (size_t)payload_length >= sizeof(payload)) return;
    // QoS 1 已确认发布时清除上一条待补结果。
    if (workcard_mqtt_publish(client, topic, payload, QOS1, 0U) == SUCCESS) {
        demo->deferred_wifi_pending = 0;
        memset(demo->deferred_wifi_topic, 0, sizeof(demo->deferred_wifi_topic));
        memset(demo->deferred_wifi_payload, 0, sizeof(demo->deferred_wifi_payload));
        return;
    }
    // 原 TCP 因 WiFi 切换失效时保存结果，等待下一次 MQTT CONNECT 后补发。
    (void)snprintf(demo->deferred_wifi_topic, sizeof(demo->deferred_wifi_topic), "%s", topic);
    (void)snprintf(demo->deferred_wifi_payload, sizeof(demo->deferred_wifi_payload), "%s", payload);
    demo->deferred_wifi_pending = 1;
}

// 在新 MQTT 会话建立后补发上次未确认的 WiFi 动作结果。
static int workcard_mqtt_flush_deferred_wifi(workcard_mqtt_demo_t *demo, MQTTClient *client)
{
    // 没有待补结果时直接成功。
    if (demo->deferred_wifi_pending == 0) return 0;
    // 只有 QoS 1 发布成功才允许清除待补状态。
    if (workcard_mqtt_publish(client, demo->deferred_wifi_topic, demo->deferred_wifi_payload, QOS1, 0U) != SUCCESS) return -1;
    demo->deferred_wifi_pending = 0;
    memset(demo->deferred_wifi_topic, 0, sizeof(demo->deferred_wifi_topic));
    memset(demo->deferred_wifi_payload, 0, sizeof(demo->deferred_wifi_payload));
    // 返回补发成功。
    return 0;
}

// 等待指定 SSID 完成关联并取得 IPv4 地址。
static int workcard_mqtt_wait_wifi(workcard_mqtt_demo_t *demo, const char *ssid)
{
    // 每秒查询一次公开 WiFi 状态，等待时间来自受控配置。
    for (unsigned int waited = 0U; waited < demo->config.network_switch_timeout_seconds; ++waited) {
        workcard_wifi_status_t status;
        memset(&status, 0, sizeof(status));
        status.struct_size = sizeof(status);
        if (workcard_wifi_get_status(demo->sdk, &status) == WORKCARD_OK && status.connected != 0U && strcmp(status.ssid, ssid) == 0 && status.ipv4_address[0] != '\0') return 0;
        sleep(1U);
    }
    return -1;
}

// 执行并上报一次附近 WiFi 扫描。
static void workcard_mqtt_process_wifi_scan(workcard_mqtt_demo_t *demo, MQTTClient *client, const char *topic_prefix, const char *payload, size_t payload_length)
{
    // 解析请求编号并拒绝格式错误命令。
    workcard_wifi_action_command_t command;
    char error[160];
    if (workcard_wifi_scan_command_parse(payload, payload_length, &command, error, sizeof(error)) != 0) {
        workcard_mqtt_publish_wifi_result(client, topic_prefix, "wifi/scan/reported", "", 1004U, error, "failed");
        return;
    }
    // 调用 SDK 同步扫描附近网络。
    workcard_wifi_scan_result_t results[WORKCARD_WIFI_MAX_SCAN_RESULTS];
    uint32_t count = 0U;
    memset(results, 0, sizeof(results));
    if (workcard_wifi_scan(demo->sdk, results, WORKCARD_WIFI_MAX_SCAN_RESULTS, &count) != WORKCARD_OK) {
        workcard_mqtt_publish_wifi_result(client, topic_prefix, "wifi/scan/reported", command.request_id, 4001U, "WiFi scan failed", "failed");
        return;
    }
    // 构造有限扫描结果数组，所有文本先执行 JSON 转义。
    char report[WORKCARD_MQTT_BUFFER_SIZE];
    int written = snprintf(report, sizeof(report), "{\"schemaVersion\":1,\"requestId\":\"%s\",\"code\":0,\"message\":\"success\",\"results\":[", command.request_id);
    for (uint32_t index = 0U; index < count && written > 0 && (size_t)written < sizeof(report); ++index) {
        char ssid[512];
        char decoded_ssid[WORKCARD_TEXT_LENGTH];
        char bssid[128];
        if (workcard_wifi_decode_scan_text(results[index].ssid, decoded_ssid, sizeof(decoded_ssid)) != 0 ||
            workcard_mqtt_json_escape(decoded_ssid, ssid, sizeof(ssid)) != 0 ||
            workcard_mqtt_json_escape(results[index].bssid, bssid, sizeof(bssid)) != 0) continue;
        written += snprintf(report + written, sizeof(report) - (size_t)written, "%s{\"ssid\":\"%s\",\"bssid\":\"%s\",\"rssiDbm\":%d,\"secured\":%s}", index == 0U ? "" : ",", ssid, bssid, results[index].rssi_dbm, results[index].secured ? "true" : "false");
    }
    if (written > 0 && (size_t)written + 3U < sizeof(report)) {
        (void)snprintf(report + written, sizeof(report) - (size_t)written, "]}");
        char topic[448];
        (void)snprintf(topic, sizeof(topic), "%s/wifi/scan/reported", topic_prefix);
        (void)workcard_mqtt_publish(client, topic, report, QOS1, 0U);
    }
}

// 应用一个 WiFi 并在成功后恢复 WiFi 默认路由。
static int workcard_mqtt_connect_wifi(workcard_mqtt_demo_t *demo, const char *ssid, const char *password, unsigned int priority)
{
    // 确保配置期间云端重连可以改走有线网关。
    if (demo->network_failover.saved == 0 && workcard_network_failover_enter(&demo->config, &demo->network_failover) != 0) return -1;
    // 将短时明文密码只复制到 SDK 调用结构。
    workcard_wifi_config_t config;
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    (void)snprintf(config.ssid, sizeof(config.ssid), "%s", ssid);
    (void)snprintf(config.password, sizeof(config.password), "%s", password);
    config.priority = priority;
    const workcard_result_t result = workcard_wifi_connect(demo->sdk, &config);
    memset(config.password, 0, sizeof(config.password));
    if (result != WORKCARD_OK || workcard_mqtt_wait_wifi(demo, ssid) != 0) return -1;
    // 目标 WiFi 已取得 IPv4 后恢复 WiFi 默认路由和原 DNS。
    workcard_network_failover_leave(&demo->config, &demo->network_failover);
    return 0;
}

// 执行一个已保存 WiFi 的即时连接动作。
static void workcard_mqtt_process_wifi_action(workcard_mqtt_demo_t *demo, MQTTClient *client, const char *topic_prefix, char *payload, size_t payload_length)
{
    // 解析短时凭据并执行受控网络切换。
    workcard_wifi_action_command_t command;
    char error[160];
    if (workcard_wifi_action_parse(payload, payload_length, &command, error, sizeof(error)) != 0) {
        workcard_mqtt_publish_wifi_result(client, topic_prefix, "wifi/action/reported", "", 1004U, error, "failed");
        memset(payload, 0, payload_length);
        return;
    }
    const int result = workcard_mqtt_connect_wifi(demo, command.ssid, command.password, command.priority);
    memset(command.password, 0, sizeof(command.password));
    memset(payload, 0, payload_length);
    workcard_mqtt_publish_wifi_action_reliable(demo, client, topic_prefix, command.request_id, result == 0 ? 0U : 4001U, result == 0 ? "success" : "WiFi connect failed; wired fallback remains active", result == 0 ? "connected" : "failed");
}

// 保存后台下发的完整 WiFi profile 集合。
static void workcard_mqtt_process_wifi_profiles(workcard_mqtt_demo_t *demo, MQTTClient *client, const char *topic_prefix, char *payload, size_t payload_length)
{
    // 解析有限 profile 数组。
    workcard_wifi_profiles_command_t command;
    char error[160];
    if (workcard_wifi_profiles_parse(payload, payload_length, &command, error, sizeof(error)) != 0) {
        workcard_mqtt_publish_wifi_result(client, topic_prefix, "wifi/profiles/reported", "", 1004U, error, "failed");
        memset(payload, 0, payload_length);
        return;
    }
    // 保存动作只同步完整配置清单，不触发 WiFi 连接或路由切换。
    if (workcard_wifi_profile_store_apply(demo->sdk, demo->config.wifi_state_file, &command, error, sizeof(error)) != 0) {
        workcard_mqtt_publish_wifi_result(client, topic_prefix, "wifi/profiles/reported", command.request_id, 4001U, error, "failed");
        memset(&command, 0, sizeof(command));
        memset(payload, 0, payload_length);
        return;
    }
    // 上报 revision 和应用结果，正文不包含密码或派生 PSK。
    char topic[448];
    char report[WORKCARD_MQTT_BUFFER_SIZE];
    (void)snprintf(topic, sizeof(topic), "%s/wifi/profiles/reported", topic_prefix);
    int written = snprintf(report, sizeof(report), "{\"schemaVersion\":1,\"requestId\":\"%s\",\"revision\":%u,\"code\":0,\"message\":\"success\",\"applyStatus\":\"applied\",\"profiles\":[", command.request_id, command.revision);
    unsigned int reported_count = 0U;
    for (unsigned int index = 0U; index < command.profile_count && written > 0 && (size_t)written < sizeof(report); ++index) {
        const workcard_wifi_command_profile_t *profile = &command.profiles[index];
        char escaped_id[192];
        char escaped_ssid[256];
        if (workcard_mqtt_json_escape(profile->profile_id, escaped_id, sizeof(escaped_id)) != 0 || workcard_mqtt_json_escape(profile->ssid, escaped_ssid, sizeof(escaped_ssid)) != 0) continue;
        written += snprintf(report + written, sizeof(report) - (size_t)written, "%s{\"profileId\":\"%s\",\"ssid\":\"%s\",\"priority\":%u,\"enabled\":%s}", reported_count == 0U ? "" : ",", escaped_id, escaped_ssid, profile->priority, profile->enabled ? "true" : "false");
        reported_count++;
    }
    if (written > 0 && (size_t)written < sizeof(report)) (void)snprintf(report + written, sizeof(report) - (size_t)written, "]}");
    (void)workcard_mqtt_publish(client, topic, report, QOS1, 1U);
    memset(&command, 0, sizeof(command));
    memset(payload, 0, payload_length);
}

// 从共享缓冲区取出并执行一条 WiFi 命令。
static void workcard_mqtt_process_wifi(workcard_mqtt_demo_t *demo, MQTTClient *client, const char *topic_prefix)
{
    // 在互斥锁下复制命令并立即清空共享凭据缓冲。
    char payload[WORKCARD_MQTT_BUFFER_SIZE];
    size_t payload_length = 0U;
    workcard_wifi_pending_type_t type = WORKCARD_WIFI_PENDING_NONE;
    pthread_mutex_lock(&demo->mutex);
    if (demo->wifi_pending_type != WORKCARD_WIFI_PENDING_NONE) {
        payload_length = demo->wifi_payload_length;
        memcpy(payload, demo->wifi_payload, payload_length + 1U);
        memset(demo->wifi_payload, 0, demo->wifi_payload_length);
        demo->wifi_payload_length = 0U;
        type = demo->wifi_pending_type;
        demo->wifi_pending_type = WORKCARD_WIFI_PENDING_NONE;
    }
    pthread_mutex_unlock(&demo->mutex);
    if (type == WORKCARD_WIFI_PENDING_PROFILES) workcard_mqtt_process_wifi_profiles(demo, client, topic_prefix, payload, payload_length);
    else if (type == WORKCARD_WIFI_PENDING_SCAN) workcard_mqtt_process_wifi_scan(demo, client, topic_prefix, payload, payload_length);
    else if (type == WORKCARD_WIFI_PENDING_ACTION) workcard_mqtt_process_wifi_action(demo, client, topic_prefix, payload, payload_length);
    memset(payload, 0, sizeof(payload));
}

// 将最后一次硬件验证成功的动态配置原子写入目标板状态文件。
static int workcard_mqtt_save_managed_state(
    const workcard_mqtt_demo_t *demo,
    const workcard_desired_config_t *desired)
{
    // 临时文件与正式文件位于同一目录，保证 rename 原子替换。
    char temporary_path[XN_CONFIG_TEXT_LENGTH + 8U];
    if (snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", demo->config.managed_state_file) <= 0) {
        return -1;
    }
    FILE *state_file = fopen(temporary_path, "w");
    if (state_file == NULL) {
        return -1;
    }

    // 使用固定键值格式保存 revision 和完整摄像头参数。
    const int write_result = fprintf(
        state_file,
        "revision=%u\nvideo_width=%u\nvideo_height=%u\nvideo_fps=%u\nvideo_gop=%u\nvideo_bitrate_bps=%u\n",
        desired->revision,
        desired->video.width,
        desired->video.height,
        desired->video.fps,
        desired->video.gop,
        desired->video.bitrate_bps);
    int save_failed = write_result <= 0 || fflush(state_file) != 0 || fsync(fileno(state_file)) != 0;
    if (fclose(state_file) != 0) {
        save_failed = 1;
    }
    if (save_failed) {
        (void)unlink(temporary_path);
        return -1;
    }
    if (chmod(temporary_path, 0600) != 0 || rename(temporary_path, demo->config.managed_state_file) != 0) {
        (void)unlink(temporary_path);
        return -1;
    }
    return 0;
}

// 在 MQTT 主循环中处理一条后台 Desired 摄像头配置。
static void workcard_mqtt_process_desired(
    workcard_mqtt_demo_t *demo,
    MQTTClient *client,
    const char *topic_prefix)
{
    // 复制待处理消息并立即释放共享锁。
    char payload[WORKCARD_MQTT_BUFFER_SIZE];
    size_t payload_length = 0U;
    pthread_mutex_lock(&demo->mutex);
    if (demo->desired_pending != 0) {
        payload_length = demo->desired_payload_length;
        memcpy(payload, demo->desired_payload, payload_length + 1U);
        demo->desired_pending = 0;
    }
    pthread_mutex_unlock(&demo->mutex);
    if (payload_length == 0U) {
        return;
    }

    // 解析完整 Desired JSON 并拒绝不完整或越界配置。
    workcard_desired_config_t desired;
    char error_message[192];
    memset(&desired, 0, sizeof(desired));
    memset(error_message, 0, sizeof(error_message));
    if (workcard_desired_config_parse(payload, payload_length, &desired, error_message, sizeof(error_message)) != 0) {
        workcard_mqtt_publish_config_result(client, topic_prefix, &desired, 2001U, error_message, "failed", 0);
        return;
    }

    // 已确认 revision 的重复消息按协议幂等成功处理。
    if (desired.revision == demo->configuration_revision) {
        workcard_mqtt_publish_config_result(client, topic_prefix, &desired, 0U, "success", "applied", 1);
        workcard_mqtt_publish_config_reported(client, topic_prefix, demo, "applied");
        return;
    }

    // 旧 revision 或错误基线禁止覆盖当前生效配置。
    const int first_retained_bootstrap = demo->configuration_revision == 0U && desired.base_revision > 0U;
    if (desired.revision < demo->configuration_revision ||
        (desired.base_revision != demo->configuration_revision && !first_retained_bootstrap)) {
        workcard_mqtt_publish_config_result(client, topic_prefix, &desired, 2002U, "配置 revision 或 baseRevision 不匹配", "failed", 0);
        return;
    }

    // 将后台配置转换为 SDK 摄像头配置结构。
    workcard_camera_config_t camera_config;
    memset(&camera_config, 0, sizeof(camera_config));
    camera_config.struct_size = sizeof(camera_config);
    camera_config.width = desired.video.width;
    camera_config.height = desired.video.height;
    camera_config.fps = desired.video.fps;
    camera_config.gop = desired.video.gop;
    camera_config.bitrate_bps = desired.video.bitrate_bps;

    // 保存回滚时重新启动上一份已验证配置所需的完整参数。
    workcard_camera_config_t previous_camera_config;
    memset(&previous_camera_config, 0, sizeof(previous_camera_config));
    previous_camera_config.struct_size = sizeof(previous_camera_config);
    previous_camera_config.width = demo->config.video_width;
    previous_camera_config.height = demo->config.video_height;
    previous_camera_config.fps = demo->config.video_fps;
    previous_camera_config.gop = demo->config.video_gop;
    previous_camera_config.bitrate_bps = demo->config.video_bitrate_bps;

    // 由主线程提供的回调负责停止、重建和验证媒体会话。
    if (demo->camera_apply_callback == NULL || demo->camera_apply_callback(&camera_config, demo->camera_apply_user_data) != 0) {
        workcard_mqtt_publish_config_result(client, topic_prefix, &desired, 4001U, "摄像头参数应用失败", "rolled_back", 0);
        workcard_mqtt_publish_config_reported(client, topic_prefix, demo, "rolled_back");
        return;
    }

    // 持久化失败时立即恢复上一份硬件已验证配置，禁止只在内存中假装成功。
    if (workcard_mqtt_save_managed_state(demo, &desired) != 0) {
        (void)demo->camera_apply_callback(&previous_camera_config, demo->camera_apply_user_data);
        workcard_mqtt_publish_config_result(client, topic_prefix, &desired, 4002U, "动态摄像头配置持久化失败", "rolled_back", 0);
        workcard_mqtt_publish_config_reported(client, topic_prefix, demo, "rolled_back");
        return;
    }

    // 只有 SDK 返回成功后才推进 revision 并向后台确认。
    demo->config.video_width = desired.video.width;
    demo->config.video_height = desired.video.height;
    demo->config.video_fps = desired.video.fps;
    demo->config.video_gop = desired.video.gop;
    demo->config.video_bitrate_bps = desired.video.bitrate_bps;
    demo->configuration_revision = desired.revision;
    workcard_mqtt_publish_config_result(client, topic_prefix, &desired, 0U, "success", "applied", 0);
    workcard_mqtt_publish_config_reported(client, topic_prefix, demo, "applied");
    fprintf(stdout, "后台摄像头配置已生效，revision=%u\n", desired.revision);
}

// 生成使用 UTC 和毫秒的 ISO 8601 时间。
static int workcard_mqtt_format_time(char *buffer, size_t buffer_size)
{
    // 读取当前 Unix 时间。
    const time_t now = time(NULL);

    // 保存线程安全的 UTC 分解时间。
    struct tm utc_time;
    memset(&utc_time, 0, sizeof(utc_time));

    // 时间转换失败时返回错误。
    if (gmtime_r(&now, &utc_time) == NULL) {
        return -1;
    }

    // 生成后台协议统一的 UTC 文本。
    return strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S.000Z", &utc_time) > 0U ? 0 : -1;
}

// 同步发布一条 MQTT 文本消息。
static int workcard_mqtt_publish(
    MQTTClient *client,
    const char *topic,
    const char *payload,
    enum QoS qos,
    unsigned char retained)
{
    // 构造 Paho 不拥有负载内存的消息结构。
    MQTTMessage message;
    memset(&message, 0, sizeof(message));
    message.qos = qos;
    message.retained = retained;
    message.payload = (void *)payload;
    message.payloadlen = strlen(payload);

    // 使用当前会话同步发布并返回 Paho 状态。
    return MQTTPublish(client, topic, &message);
}

// 构建不含敏感字段的设备在线状态。
static int workcard_mqtt_build_presence(
    const workcard_mqtt_demo_t *demo,
    int online,
    const char *reason,
    char *buffer,
    size_t buffer_size)
{
    // 在线消息需要稳定的连接时间。
    char timestamp[32];
    memset(timestamp, 0, sizeof(timestamp));

    // 时间生成失败时拒绝发布伪造时间。
    if (workcard_mqtt_format_time(timestamp, sizeof(timestamp)) != 0) {
        return -1;
    }

    // 在线时发布连接时间和客户应用版本。
    if (online) {
        return snprintf(
                   buffer,
                   buffer_size,
                   "{\"schemaVersion\":1,\"online\":true,\"connectedAt\":\"%s\",\"firmwareVersion\":\"%s\"}",
                   timestamp,
                   demo->config.firmware_version) > 0
            ? 0
            : -1;
    }

    // 离线时只发布后台定义的非敏感原因。
    return snprintf(
               buffer,
               buffer_size,
               "{\"schemaVersion\":1,\"online\":false,\"reason\":\"%s\"}",
               reason) > 0
        ? 0
        : -1;
}

// 发布当前 SDK 可查询的运行状态。
static int workcard_mqtt_publish_state(
    workcard_mqtt_demo_t *demo,
    MQTTClient *client,
    const char *topic,
    unsigned int sequence)
{
    // 初始化 WiFi 状态输出结构。
    workcard_wifi_status_t wifi_status;
    memset(&wifi_status, 0, sizeof(wifi_status));
    wifi_status.struct_size = sizeof(wifi_status);

    // SDK 断开时明确上报网络不可查询。
    const int wifi_connected = workcard_wifi_get_status(demo->sdk, &wifi_status) == WORKCARD_OK &&
        wifi_status.connected != 0U;

    // 查询有线接口的链路和 IPv4，切网失败时仍准确上报云端出口。
    workcard_network_status_t ethernet_status;
    memset(&ethernet_status, 0, sizeof(ethernet_status));
    ethernet_status.struct_size = sizeof(ethernet_status);
    const int ethernet_connected = workcard_network_get_status(demo->sdk, WORKCARD_NETWORK_ETHERNET, &ethernet_status) == WORKCARD_OK &&
        ethernet_status.link_up != 0U && ethernet_status.has_ipv4 != 0U;
    // 有线保活状态存在时优先报告有线为当前业务出口。
    const int using_ethernet = demo->network_failover.saved != 0 && ethernet_connected;

    // 生成后台接收时间。
    char timestamp[32];
    memset(timestamp, 0, sizeof(timestamp));
    if (workcard_mqtt_format_time(timestamp, sizeof(timestamp)) != 0) {
        return -1;
    }

    // 对 WiFi 公开文本执行 JSON 转义。
    char wifi_ssid[512];
    char wifi_bssid[128];
    char wifi_ipv4[128];
    char ethernet_ipv4[128];
    if (workcard_mqtt_json_escape(wifi_status.ssid, wifi_ssid, sizeof(wifi_ssid)) != 0 ||
        workcard_mqtt_json_escape(wifi_status.bssid, wifi_bssid, sizeof(wifi_bssid)) != 0 ||
        workcard_mqtt_json_escape(wifi_status.ipv4_address, wifi_ipv4, sizeof(wifi_ipv4)) != 0 ||
        workcard_mqtt_json_escape(ethernet_status.ipv4_address, ethernet_ipv4, sizeof(ethernet_ipv4)) != 0) return -1;

    // 构造字段稳定且不含 WiFi 密码的完整状态快照。
    char payload[1536];
    const int payload_length = snprintf(
        payload,
        sizeof(payload),
        "{\"schemaVersion\":1,\"sequence\":%u,\"reportedAt\":\"%s\","
        "\"system\":{\"status\":\"running\",\"firmwareVersion\":\"%s\"},"
        "\"network\":{\"status\":\"%s\",\"activeInterface\":%s,"
        "\"wifi\":{\"connected\":%s,\"ssid\":\"%s\",\"bssid\":\"%s\",\"ipv4Address\":\"%s\",\"rssiDbm\":%d},\"ethernet\":{\"connected\":%s,\"ipv4Address\":\"%s\"},"
        "\"cellular\":{\"status\":\"unsupported\"}},"
        "\"battery\":{\"status\":\"unsupported\"},"
        "\"location\":{\"status\":\"unsupported\"},"
        "\"media\":{\"status\":\"running\",\"video\":{\"width\":%u,\"height\":%u,\"fps\":%u,\"gop\":%u,\"bitrateBps\":%u,\"codec\":\"h264-baseline\"},"
        "\"audio\":{\"sampleRate\":%u,\"channels\":%u,\"bitrateBps\":64000,\"captureVolume\":30,\"playbackVolume\":3,\"codec\":\"opus\"}}}",
        sequence,
        timestamp,
        demo->config.firmware_version,
        (wifi_connected || ethernet_connected) ? "connected" : "disconnected",
        using_ethernet ? "\"ethernet\"" : (wifi_connected ? "\"wifi\"" : (ethernet_connected ? "\"ethernet\"" : "null")),
        wifi_connected ? "true" : "false",
        wifi_ssid,
        wifi_bssid,
        wifi_ipv4,
        wifi_status.rssi_dbm,
        ethernet_connected ? "true" : "false",
        ethernet_ipv4,
        demo->config.video_width,
        demo->config.video_height,
        demo->config.video_fps,
        demo->config.video_gop,
        demo->config.video_bitrate_bps,
        demo->config.audio_sample_rate,
        demo->config.audio_channels);

    // 拒绝发布被截断的 JSON。
    if (payload_length <= 0 || (size_t)payload_length >= sizeof(payload)) {
        return -1;
    }

    // 使用 QoS 1 和 Retained 保留最新完整状态。
    return workcard_mqtt_publish(client, topic, payload, QOS1, 1U);
}

// 运行单次 MQTT TLS 会话直到断线或 Demo 停止。
static int workcard_mqtt_run_session(workcard_mqtt_demo_t *demo)
{
    // 初始化 Paho 使用的 TLS 网络适配。
    Network network;
    xn_mqtt_network_init(&network);

    // 建立并校验 Broker TLS 连接。
    char network_error[256];
    memset(network_error, 0, sizeof(network_error));
    if (xn_mqtt_network_connect(
            &network,
            demo->config.mqtt_host,
            (unsigned short)demo->config.mqtt_port,
            demo->config.ca_file,
            network_error,
            sizeof(network_error)) != 0) {
        fprintf(stderr, "MQTT连接失败: %s\n", network_error);
        return -1;
    }

    // 使用固定大小栈缓冲区初始化 Paho 客户端。
    unsigned char send_buffer[WORKCARD_MQTT_BUFFER_SIZE];
    unsigned char receive_buffer[WORKCARD_MQTT_BUFFER_SIZE];
    MQTTClient client;
    MQTTClientInit(
        &client,
        &network,
        5000U,
        send_buffer,
        sizeof(send_buffer),
        receive_buffer,
        sizeof(receive_buffer));

    // 构造当前设备独占的 Topic 和 Client ID。
    char topic_prefix[384];
    char presence_topic[448];
    char capabilities_topic[448];
    char state_topic[448];
    char config_desired_topic[448];
    char wifi_profiles_topic[448];
    char wifi_scan_topic[448];
    char wifi_action_topic[448];
    char client_id[320];
    snprintf(topic_prefix, sizeof(topic_prefix), "v1/devices/%s", demo->config.device_id);
    snprintf(presence_topic, sizeof(presence_topic), "%s/presence", topic_prefix);
    snprintf(capabilities_topic, sizeof(capabilities_topic), "%s/capabilities", topic_prefix);
    snprintf(state_topic, sizeof(state_topic), "%s/state/reported", topic_prefix);
    snprintf(config_desired_topic, sizeof(config_desired_topic), "%s/config/desired", topic_prefix);
    snprintf(wifi_profiles_topic, sizeof(wifi_profiles_topic), "%s/wifi/profiles/set", topic_prefix);
    snprintf(wifi_scan_topic, sizeof(wifi_scan_topic), "%s/wifi/scan/command", topic_prefix);
    snprintf(wifi_action_topic, sizeof(wifi_action_topic), "%s/wifi/action/command", topic_prefix);
    snprintf(client_id, sizeof(client_id), "lite-p-%s", demo->config.device_id);

    // 构造 Broker 在异常断线时发布的 Retained 离线遗嘱。
    char will_payload[256];
    if (workcard_mqtt_build_presence(
            demo,
            0,
            "connection_lost",
            will_payload,
            sizeof(will_payload)) != 0) {
        xn_mqtt_network_disconnect(&network);
        return -1;
    }

    // 配置 MQTT 3.1.1、设备独立账号和六十秒 KeepAlive。
    MQTTPacket_connectData connect_data = MQTTPacket_connectData_initializer;
    connect_data.MQTTVersion = 4;
    connect_data.clientID.cstring = client_id;
    connect_data.username.cstring = demo->config.mqtt_username;
    connect_data.password.cstring = demo->config.mqtt_password;
    connect_data.keepAliveInterval = 60;
    connect_data.cleansession = 0;
    connect_data.willFlag = 1;
    connect_data.will.topicName.cstring = presence_topic;
    connect_data.will.message.cstring = will_payload;
    connect_data.will.qos = 1;
    connect_data.will.retained = 1;

    // 完成 Broker 用户认证和 CONNACK 校验。
    if (MQTTConnect(&client, &connect_data) != SUCCESS) {
        fprintf(stderr, "MQTT连接失败: Broker认证或CONNACK失败\n");
        xn_mqtt_network_disconnect(&network);
        return -1;
    }

    // 注册当前 Demo 的 Desired 回调并订阅后台 Retained 配置。
    workcard_mqtt_callback_demo = demo;
    if (MQTTSubscribe(&client, config_desired_topic, QOS1, workcard_mqtt_message_arrived) != SUCCESS ||
        MQTTSubscribe(&client, wifi_profiles_topic, QOS1, workcard_mqtt_message_arrived) != SUCCESS ||
        MQTTSubscribe(&client, wifi_scan_topic, QOS1, workcard_mqtt_message_arrived) != SUCCESS ||
        MQTTSubscribe(&client, wifi_action_topic, QOS1, workcard_mqtt_message_arrived) != SUCCESS) {
        workcard_mqtt_callback_demo = NULL;
        MQTTDisconnect(&client);
        xn_mqtt_network_disconnect(&network);
        fprintf(stderr, "MQTT配置 Topic 订阅失败\n");
        return -1;
    }

    // 发布 Retained 在线状态。
    char online_payload[320];
    if (workcard_mqtt_build_presence(
            demo,
            1,
            NULL,
            online_payload,
            sizeof(online_payload)) != 0 ||
        workcard_mqtt_publish(&client, presence_topic, online_payload, QOS1, 1U) != SUCCESS) {
        MQTTDisconnect(&client);
        xn_mqtt_network_disconnect(&network);
        return -1;
    }

    // 在线状态建立后优先补发网络切换期间未确认的动作结果。
    if (workcard_mqtt_flush_deferred_wifi(demo, &client) != 0) {
        MQTTDisconnect(&client);
        xn_mqtt_network_disconnect(&network);
        return -1;
    }

    // 发布当前 Demo 已经实际接入的能力快照。
    const char *capabilities_payload =
        "{\"schemaVersion\":1,\"deviceModel\":\"HongOu-Lite-P\",\"hardwareRevision\":\"V1.0\",\"modules\":{"
        "\"video\":{\"supported\":true,\"codec\":[\"h264-baseline\"],\"constraints\":{"
        "\"width\":{\"minimum\":160,\"maximum\":1600,\"step\":2},"
        "\"height\":{\"minimum\":120,\"maximum\":1200,\"step\":2},"
        "\"fps\":{\"minimum\":1,\"maximum\":30,\"step\":1},"
        "\"gop\":{\"minimum\":1,\"maximum\":300,\"step\":1},"
        "\"bitrateBps\":{\"minimum\":64000,\"maximum\":4000000,\"step\":1000}},"
        "\"applyMode\":\"next-media-session\"},"
        "\"audio\":{\"supported\":true,\"codec\":[\"opus\"],\"sampleRates\":[16000],\"channels\":[1]},"
        "\"wifi\":{\"supported\":true,\"security\":[\"wpa2-psk\"]},"
        "\"bluetooth\":{\"supported\":true,\"profiles\":[\"a2dp-source\"]},"
        "\"battery\":{\"supported\":false},\"location\":{\"supported\":false,\"sources\":[]},"
        "\"cellular\":{\"supported\":false},\"ota\":{\"supported\":false}}}";
    if (workcard_mqtt_publish(&client, capabilities_topic, capabilities_payload, QOS1, 1U) != SUCCESS) {
        MQTTDisconnect(&client);
        xn_mqtt_network_disconnect(&network);
        return -1;
    }

    // 立即发布首次状态并从一开始递增序号。
    unsigned int sequence = 1U;
    if (workcard_mqtt_publish_state(demo, &client, state_topic, sequence) != SUCCESS) {
        MQTTDisconnect(&client);
        xn_mqtt_network_disconnect(&network);
        return -1;
    }

    // 首次连接先发布当前本地生效配置，后台据此建立 Reported 基线。
    workcard_mqtt_publish_config_reported(&client, topic_prefix, demo, "applied");

    // 输出不含 MQTT 密码的成功状态。
    fprintf(stdout, "MQTT连接成功，设备在线状态已发布\n");

    // 保存上次周期状态时间。
    time_t last_state_time = time(NULL);

    // 持续处理 MQTT ACK、KeepAlive 和周期状态。
    while (workcard_mqtt_is_running(demo) && client.isconnected != 0) {
        // 每秒让 Paho 处理网络输入和 PINGREQ。
        if (MQTTYield(&client, 1000) != SUCCESS) {
            break;
        }

        // 在网络线程上下文中串行应用待处理摄像头配置。
        workcard_mqtt_process_desired(demo, &client, topic_prefix);

        // 在同一线程串行处理 WiFi 扫描、保存和连接命令。
        workcard_mqtt_process_wifi(demo, &client, topic_prefix);

        // 到达周期时发布新的 Retained 状态。
        const time_t current_time = time(NULL);
        if (current_time - last_state_time >= (time_t)WORKCARD_MQTT_STATE_INTERVAL_SECONDS) {
            sequence++;
            if (workcard_mqtt_publish_state(demo, &client, state_topic, sequence) != SUCCESS) {
                break;
            }
            last_state_time = current_time;
        }
    }

    // Demo 正常停止时主动发布 Retained 离线状态。
    if (!workcard_mqtt_is_running(demo) && client.isconnected != 0) {
        char offline_payload[256];
        if (workcard_mqtt_build_presence(
                demo,
                0,
                "normal_shutdown",
                offline_payload,
                sizeof(offline_payload)) == 0) {
            (void)workcard_mqtt_publish(&client, presence_topic, offline_payload, QOS1, 1U);
        }
    }

    // 释放 MQTT 协议会话和 TLS/TCP 连接。
    MQTTDisconnect(&client);
    workcard_mqtt_callback_demo = NULL;
    xn_mqtt_network_disconnect(&network);

    // 断线时返回失败以触发重连，主动停止时返回成功。
    return workcard_mqtt_is_running(demo) ? -1 : 0;
}

// 持续维护 MQTT 会话并使用有上限的重连等待。
static void *workcard_mqtt_thread_main(void *argument)
{
    // 恢复 MQTT Demo 上下文。
    workcard_mqtt_demo_t *demo = argument;

    // 从一秒开始重连并最多等待四秒。
    unsigned int reconnect_delay = 1U;
    while (workcard_mqtt_is_running(demo)) {
        // 单次会话成功运行后恢复最小重连等待。
        if (workcard_mqtt_run_session(demo) == 0) {
            break;
        }

        // 输出不含凭据的重连状态。
        if (workcard_mqtt_is_running(demo)) {
            fprintf(stderr, "MQTT会话已断开，%u秒后重连\n", reconnect_delay);
            sleep(reconnect_delay);
            reconnect_delay = reconnect_delay < 4U ? reconnect_delay * 2U : 4U;
        }
    }

    // 结束 MQTT 工作线程。
    return NULL;
}

// 创建 MQTT Demo 对象。
int workcard_mqtt_demo_create(
    workcard_sdk_t *sdk,
    const xn_app_config *config,
    workcard_mqtt_demo_t **demo_pointer,
    workcard_mqtt_camera_apply_callback camera_apply_callback,
    void *camera_apply_user_data)
{
    // 要求 SDK、配置和输出指针全部有效。
    if (sdk == NULL || config == NULL || demo_pointer == NULL) {
        return -1;
    }

    // 分配并清零 MQTT Demo 状态。
    workcard_mqtt_demo_t *demo = calloc(1U, sizeof(*demo));
    if (demo == NULL) {
        return -1;
    }

    // 保存 SDK 句柄和不含指针的配置副本。
    demo->sdk = sdk;
    demo->config = *config;
    demo->configuration_revision = config->managed_revision;
    demo->camera_apply_callback = camera_apply_callback;
    demo->camera_apply_user_data = camera_apply_user_data;

    // 初始化线程状态互斥锁。
    if (pthread_mutex_init(&demo->mutex, NULL) != 0) {
        free(demo);
        return -1;
    }

    // 返回创建完成的 MQTT Demo 对象。
    *demo_pointer = demo;
    return 0;
}

// 启动 MQTT Demo 工作线程。
int workcard_mqtt_demo_start(workcard_mqtt_demo_t *demo)
{
    // 空对象或重复启动属于调用错误。
    if (demo == NULL || demo->thread_started) {
        return -1;
    }

    // 在锁保护下允许工作线程运行。
    pthread_mutex_lock(&demo->mutex);
    demo->running = 1;
    pthread_mutex_unlock(&demo->mutex);

    // 创建唯一 MQTT 网络线程。
    if (pthread_create(&demo->thread, NULL, workcard_mqtt_thread_main, demo) != 0) {
        pthread_mutex_lock(&demo->mutex);
        demo->running = 0;
        pthread_mutex_unlock(&demo->mutex);
        return -1;
    }

    // 标记停止阶段必须回收线程。
    demo->thread_started = 1;
    return 0;
}

// 停止 MQTT Demo 工作线程。
void workcard_mqtt_demo_stop(workcard_mqtt_demo_t *demo)
{
    // 空对象无需停止。
    if (demo == NULL) {
        return;
    }

    // 在锁保护下请求网络循环退出。
    pthread_mutex_lock(&demo->mutex);
    demo->running = 0;
    pthread_mutex_unlock(&demo->mutex);

    // 回收已经创建的 MQTT 线程。
    if (demo->thread_started) {
        pthread_join(demo->thread, NULL);
        demo->thread_started = 0;
    }
}

// 销毁 MQTT Demo 对象。
void workcard_mqtt_demo_destroy(workcard_mqtt_demo_t *demo)
{
    // 空对象允许安全重复清理。
    if (demo == NULL) {
        return;
    }

    // 确保网络线程不再访问对象。
    workcard_mqtt_demo_stop(demo);

    // 销毁同步资源并释放对象。
    pthread_mutex_destroy(&demo->mutex);
    free(demo);
}
