#ifndef WORKCARD_MQTT_DEMO_H
#define WORKCARD_MQTT_DEMO_H

// 引入底层 SDK 句柄和 Demo 配置类型。
#include <workcard/workcard_sdk.h>

// 引入 Demo 统一配置结构。
#include "cloud/internal/config.h"

// 隐藏 MQTT Demo 内部线程、TLS 和协议状态。
typedef struct workcard_mqtt_demo workcard_mqtt_demo_t;

// 定义应用层重新应用摄像头配置的回调函数类型。
typedef int (*workcard_mqtt_camera_apply_callback)(
    const workcard_camera_config_t *config,
    void *user_data);

// 创建应用层 MQTT 示例对象，但尚不建立网络连接。
int workcard_mqtt_demo_create(
    workcard_sdk_t *sdk,
    const xn_app_config *config,
    workcard_mqtt_demo_t **demo_pointer,
    workcard_mqtt_camera_apply_callback camera_apply_callback,
    void *camera_apply_user_data);

// 启动 MQTT TLS、在线状态、遗嘱和周期状态上报线程。
int workcard_mqtt_demo_start(workcard_mqtt_demo_t *demo);

// 正常停止 MQTT 线程并尽力发布主动离线状态。
void workcard_mqtt_demo_stop(workcard_mqtt_demo_t *demo);

// 销毁 MQTT Demo 对象和全部线程同步资源。
void workcard_mqtt_demo_destroy(workcard_mqtt_demo_t *demo);

#endif
