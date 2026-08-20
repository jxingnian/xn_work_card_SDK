// 引入客户使用的完整底层 SDK API。
#include <workcard/workcard_sdk.h>

// 引入 Demo 云端参考实现和配置解析器。
#include "cloud/cloud_demo.h"
#include "cloud/internal/config.h"
#include "mqtt/mqtt_demo.h"
// 引入经典蓝牙 API 的独立参考例程。
#include "bluetooth/bluetooth_demo.h"

// 引入信号、输入输出和进程接口。
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 保存进程级退出标志，信号处理器只修改该原子类型。
static volatile sig_atomic_t workcard_demo_stop_requested = 0;

// 保存底层服务断开后的会话重建标志。
static volatile sig_atomic_t workcard_demo_sdk_disconnected = 0;

// 保存 Demo 回调共享的云端参考客户端。
static workcard_cloud_demo_t *workcard_demo_cloud = NULL;

// 保存应用层 MQTT 参考客户端。
static workcard_mqtt_demo_t *workcard_demo_mqtt = NULL;

// 保存动态摄像头配置回调所需的应用上下文。
typedef struct {
    // 保存底层 SDK 句柄。
    workcard_sdk_t *sdk;
    // 保存云端参考客户端。
    workcard_cloud_demo_t *cloud;
    // 保存应用配置，成功切换后同步更新。
    xn_app_config *config;
} workcard_demo_media_context_t;

// 串行化摄像头停止、重建和配置更新流程。
static pthread_mutex_t workcard_demo_media_mutex = PTHREAD_MUTEX_INITIALIZER;

// 前置声明视频回调，供动态媒体重建函数注册。
static void workcard_demo_video_callback(const workcard_video_frame_t *frame, void *user_data);

// 应用后台下发的摄像头配置并验证下一媒体会话。
static int workcard_demo_apply_camera_config(const workcard_camera_config_t *camera_config, void *user_data)
{
    // 恢复动态配置上下文。
    workcard_demo_media_context_t *context = user_data;
    if (context == NULL || context->sdk == NULL || context->cloud == NULL || context->config == NULL || camera_config == NULL) {
        return -1;
    }

    // 使用互斥锁保证同一时刻只有一条线程重建媒体链路。
    pthread_mutex_lock(&workcard_demo_media_mutex);
    (void)workcard_camera_stop(context->sdk);
    const workcard_result_t result = workcard_camera_start(
        context->sdk,
        camera_config,
        workcard_demo_video_callback,
        context->cloud);
    if (result == WORKCARD_OK) {
        // 只有硬件确认启动成功后才同步应用层配置。
        context->config->video_width = camera_config->width;
        context->config->video_height = camera_config->height;
        context->config->video_fps = camera_config->fps;
        context->config->video_gop = camera_config->gop;
        context->config->video_bitrate_bps = camera_config->bitrate_bps;
        // 请求 WSS 在下一轮鉴权中刷新后台设备媒体参数展示。
        workcard_cloud_demo_update_video_config(context->cloud, camera_config);
    }
    pthread_mutex_unlock(&workcard_demo_media_mutex);
    return result == WORKCARD_OK ? 0 : -1;
}

// 处理系统正常终止信号。
static void workcard_demo_signal_handler(int signal_number)
{
    // 信号编号只用于满足处理器签名。
    (void)signal_number;

    // 请求主循环按顺序释放云端和底层资源。
    workcard_demo_stop_requested = 1;
}

// 打印底层 SDK 通用事件。
static void workcard_demo_event_callback(const workcard_event_t *event, void *user_data)
{
    // 当前 Demo 不需要额外客户上下文。
    (void)user_data;

    // 空事件不进行处理。
    if (event == NULL) {
        return;
    }

    // 输出不包含设备凭据的模块状态。
    fprintf(
        event->result == WORKCARD_OK ? stdout : stderr,
        "SDK事件: type=%d module=%d result=%d message=%s\n",
        (int)event->type,
        (int)event->module,
        (int)event->result,
        event->message);

    // 底层服务连接断开时通知主线程完成有序清理并重建整个应用会话。
    if (event->type == WORKCARD_EVENT_SERVICE_DISCONNECTED) {
        workcard_demo_sdk_disconnected = 1;
    }
}

// 接收 SDK 的 H.264 数据并交给应用层云端参考实现。
static void workcard_demo_video_callback(const workcard_video_frame_t *frame, void *user_data)
{
    // 客户上下文保存云端参考客户端。
    workcard_cloud_demo_t *cloud = user_data;

    // 云端模块自行判断鉴权和观看门控。
    workcard_cloud_demo_submit_video(cloud, frame);
}

// 接收 SDK 的 Opus 数据并交给应用层云端参考实现。
static void workcard_demo_audio_callback(const workcard_audio_frame_t *frame, void *user_data)
{
    // 客户上下文保存云端参考客户端。
    workcard_cloud_demo_t *cloud = user_data;

    // 云端模块自行判断鉴权和观看门控。
    workcard_cloud_demo_submit_audio(cloud, frame);
}

// 打印当前固定固件的模块能力。
static void workcard_demo_print_capabilities(workcard_sdk_t *sdk)
{
    // 逐个查询当前规划的九个模块。
    for (int module = WORKCARD_MODULE_DEVICE; module <= WORKCARD_MODULE_IMU; ++module) {
        workcard_module_capability_t capability;
        memset(&capability, 0, sizeof(capability));
        capability.struct_size = sizeof(capability);
        if (workcard_sdk_get_module_capability(
                sdk,
                (workcard_module_t)module,
                &capability) == WORKCARD_OK) {
            fprintf(stdout, "模块能力: module=%d supported=%u\n", module, capability.supported);
        }
    }
}

// 工牌参考应用入口。
int main(int argument_count, char **argument_values)
{
    // 标准输出使用行缓冲，保证后台重定向日志能及时看到每条状态。
    (void)setvbuf(stdout, NULL, _IOLBF, 0U);

    // 标准错误关闭缓冲，保证故障信息在进程异常退出前已经落盘。
    (void)setvbuf(stderr, NULL, _IONBF, 0U);

    // 默认读取固定部署目录中的 UTF-8 配置。
    const char *config_path = "/opt/workcard-demo/workcard-demo.conf";
    // 非空时只运行蓝牙 API 参考流程，便于客户逐项验证硬件。
    const char *bluetooth_demo_address = NULL;

    // 允许客户在开发阶段显式指定配置文件。
    if (argument_count == 3 && strcmp(argument_values[1], "--config") == 0) {
        config_path = argument_values[2];
    } else if ((argument_count == 2 || argument_count == 3) &&
               strcmp(argument_values[1], "--bluetooth-demo") == 0) {
        bluetooth_demo_address = argument_count == 3 ? argument_values[2] : NULL;
    } else if (argument_count != 1) {
        fprintf(stderr, "用法: %s [--config 配置文件路径] [--bluetooth-demo [MAC]]\n", argument_values[0]);
        return EXIT_FAILURE;
    }

    // 读取并严格校验当前云端和媒体配置。
    xn_app_config app_config;
    char config_error[256];
    memset(&app_config, 0, sizeof(app_config));
    memset(config_error, 0, sizeof(config_error));
    if (xn_config_load(config_path, &app_config, config_error, sizeof(config_error)) != 0) {
        fprintf(stderr, "Demo配置错误: %s\n", config_error);
        return EXIT_FAILURE;
    }

    // 创建只包含本地服务地址和事件回调的 SDK 配置。
    workcard_sdk_config_t sdk_config;
    memset(&sdk_config, 0, sizeof(sdk_config));
    sdk_config.struct_size = sizeof(sdk_config);
    sdk_config.request_timeout_ms = 45000U;
    sdk_config.event_callback = workcard_demo_event_callback;

    // 创建并连接底层 SDK 服务。
    workcard_sdk_t *sdk = NULL;
    workcard_result_t result = workcard_sdk_create(&sdk_config, &sdk);
    if (result != WORKCARD_OK) {
        fprintf(stderr, "SDK启动失败: %s\n", workcard_result_string(result));
        workcard_sdk_destroy(sdk);
        return EXIT_FAILURE;
    }

    // 服务异常恢复期间最多等待三十秒，避免 supervisor 尚未发布 Socket 时直接退出。
    unsigned int sdk_connect_waited_seconds = 0U;
    while ((result = workcard_sdk_start(sdk)) != WORKCARD_OK &&
           !workcard_demo_stop_requested &&
           sdk_connect_waited_seconds < 30U) {
        sleep(1U);
        sdk_connect_waited_seconds++;
    }
    if (result != WORKCARD_OK) {
        fprintf(stderr, "SDK启动失败: %s\n", workcard_result_string(result));
        workcard_sdk_destroy(sdk);
        return EXIT_FAILURE;
    }

    // 查询并输出不包含敏感信息的设备版本。
    workcard_device_info_t device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.struct_size = sizeof(device_info);
    if (workcard_sdk_get_device_info(sdk, &device_info) == WORKCARD_OK) {
        fprintf(
            stdout,
            "设备=%s 硬件=%s 固件=%s SDK=%s ABI=%u.%u\n",
            device_info.device_model,
            device_info.hardware_revision,
            device_info.firmware_version,
            device_info.sdk_version,
            device_info.abi_version_major,
            device_info.abi_version_minor);
    }
    workcard_demo_print_capabilities(sdk);

    // 蓝牙独立例程只验证公开 API，完成后释放 SDK 并退出，不启动云端媒体会话。
    if (bluetooth_demo_address != NULL || (argument_count >= 2 && strcmp(argument_values[1], "--bluetooth-demo") == 0)) {
        const int bluetooth_demo_result = workcard_bluetooth_demo_run(sdk, bluetooth_demo_address);
        workcard_sdk_stop(sdk);
        workcard_sdk_destroy(sdk);
        return bluetooth_demo_result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // 创建完全位于应用层的当前云端参考实现。
    if (workcard_cloud_demo_create(sdk, &app_config, &workcard_demo_cloud) != 0) {
        fprintf(stderr, "云端参考客户端创建失败\n");
        workcard_sdk_destroy(sdk);
        return EXIT_FAILURE;
    }

    // 注册 Opus 采集回调。
    result = workcard_audio_set_capture_callback(
        sdk,
        workcard_demo_audio_callback,
        workcard_demo_cloud);
    if (result != WORKCARD_OK) {
        fprintf(stderr, "音频回调注册失败: %s\n", workcard_result_string(result));
        workcard_cloud_demo_destroy(workcard_demo_cloud);
        workcard_sdk_destroy(sdk);
        return EXIT_FAILURE;
    }

    // 使用配置文件中的参数启动 H.264 和统一媒体链路。
    workcard_camera_config_t camera_config;
    memset(&camera_config, 0, sizeof(camera_config));
    camera_config.struct_size = sizeof(camera_config);
    camera_config.width = app_config.video_width;
    camera_config.height = app_config.video_height;
    camera_config.fps = app_config.video_fps;
    camera_config.gop = app_config.video_gop;
    camera_config.bitrate_bps = app_config.video_bitrate_bps;
    result = workcard_camera_start(
        sdk,
        &camera_config,
        workcard_demo_video_callback,
        workcard_demo_cloud);
    if (result != WORKCARD_OK) {
        fprintf(stderr, "摄像头启动失败: %s\n", workcard_result_string(result));
        workcard_cloud_demo_destroy(workcard_demo_cloud);
        workcard_sdk_destroy(sdk);
        return EXIT_FAILURE;
    }

    // 保存动态配置回调使用的 SDK、云端和应用配置上下文。
    workcard_demo_media_context_t media_context;
    media_context.sdk = sdk;
    media_context.cloud = workcard_demo_cloud;
    media_context.config = &app_config;

    // 启动应用层 WSS 连接、鉴权、心跳和重连。
    if (workcard_cloud_demo_start(workcard_demo_cloud) != 0) {
        fprintf(stderr, "云端参考客户端启动失败\n");
        (void)workcard_camera_stop(sdk);
        workcard_cloud_demo_destroy(workcard_demo_cloud);
        workcard_sdk_destroy(sdk);
        return EXIT_FAILURE;
    }

    // 创建完全位于应用层的 MQTT 参考实现。
    if (workcard_mqtt_demo_create(
            sdk,
            &app_config,
            &workcard_demo_mqtt,
            workcard_demo_apply_camera_config,
            &media_context) != 0 ||
        workcard_mqtt_demo_start(workcard_demo_mqtt) != 0) {
        // MQTT 是后台在线状态的正式上报通道，启动失败时拒绝继续运行假在线 Demo。
        fprintf(stderr, "MQTT参考客户端启动失败，Demo停止\n");
        workcard_mqtt_demo_destroy(workcard_demo_mqtt);
        workcard_demo_mqtt = NULL;
        workcard_cloud_demo_stop(workcard_demo_cloud);
        (void)workcard_camera_stop(sdk);
        workcard_cloud_demo_destroy(workcard_demo_cloud);
        workcard_sdk_destroy(sdk);
        return EXIT_FAILURE;
    }

    // 注册正常退出信号并忽略底层网络断开产生的 SIGPIPE。
    signal(SIGINT, workcard_demo_signal_handler);
    signal(SIGTERM, workcard_demo_signal_handler);
    signal(SIGPIPE, SIG_IGN);

    // 主线程只负责等待退出，硬件和云端工作分别由 SDK 与 Demo 模块管理。
    while (!workcard_demo_stop_requested && !workcard_demo_sdk_disconnected) {
        sleep(1U);
    }

    // 先停止云端，保证不再调用媒体回调发送网络数据。
    workcard_cloud_demo_stop(workcard_demo_cloud);

    // 再停止 MQTT 并主动发布 Retained 离线状态。
    workcard_mqtt_demo_stop(workcard_demo_mqtt);

    // 再停止摄像头、音频采集和播放硬件。
    (void)workcard_audio_set_playback_enabled(sdk, 0U);
    (void)workcard_camera_stop(sdk);

    // 最后销毁应用层云端和底层 SDK 状态。
    workcard_cloud_demo_destroy(workcard_demo_cloud);
    workcard_demo_cloud = NULL;
    workcard_mqtt_demo_destroy(workcard_demo_mqtt);
    workcard_demo_mqtt = NULL;
    workcard_sdk_destroy(sdk);

    // 服务异常断开时使用同一参数原地重启进程，完整重建 SDK、媒体、WSS 和 MQTT 状态。
    if (workcard_demo_sdk_disconnected && !workcard_demo_stop_requested) {
        fprintf(stderr, "底层SDK服务已断开，2秒后重建Demo会话\n");
        sleep(2U);
        execv(argument_values[0], argument_values);
        fprintf(stderr, "Demo会话重建失败: 无法重新执行当前程序\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
