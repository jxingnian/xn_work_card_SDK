#ifndef WORKCARD_SDK_H
#define WORKCARD_SDK_H

// 汇总公开模块头文件，客户只引用本文件即可使用完整 SDK。
#include "workcard_audio.h"
#include "workcard_bluetooth.h"
#include "workcard_camera.h"
#include "workcard_gnss.h"
#include "workcard_imu.h"
#include "workcard_network.h"
#include "workcard_types.h"
#include "workcard_wifi.h"

// 允许公开头文件同时被 C 和 C++ 客户程序引用。
#ifdef __cplusplus
extern "C" {
#endif

// 声明客户只能通过公开函数管理的不透明 SDK 句柄。
typedef struct workcard_sdk workcard_sdk_t;

// 保存 SDK 创建参数。
typedef struct workcard_sdk_config {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 保存底层服务 Unix Socket 路径，空字符串表示使用正式默认路径。
    char service_socket_path[WORKCARD_TEXT_LENGTH];

    // 保存同步 API 的响应超时毫秒数，零表示使用正式默认值。
    uint32_t request_timeout_ms;

    // 保存通用事件回调，允许为空。
    workcard_event_callback_t event_callback;

    // 保存原样传回事件回调的客户上下文。
    void *event_user_data;
} workcard_sdk_config_t;

// 创建 SDK 句柄但不连接底层服务。
workcard_result_t workcard_sdk_create(
    const workcard_sdk_config_t *config,
    workcard_sdk_t **sdk);

// 连接固定固件中运行的底层 SDK 服务。
workcard_result_t workcard_sdk_start(workcard_sdk_t *sdk);

// 查询设备、固件、SDK 和 ABI 版本信息。
workcard_result_t workcard_sdk_get_device_info(workcard_sdk_t *sdk, workcard_device_info_t *info);

// 查询指定模块在当前固件中的实际支持状态。
workcard_result_t workcard_sdk_get_module_capability(
    workcard_sdk_t *sdk,
    workcard_module_t module,
    workcard_module_capability_t *capability);

// 停止回调线程并断开底层服务，允许安全重复调用。
void workcard_sdk_stop(workcard_sdk_t *sdk);

// 销毁 SDK 句柄，调用后客户不得再次访问该指针。
void workcard_sdk_destroy(workcard_sdk_t *sdk);

// 返回不包含敏感信息的静态错误码说明文本。
const char *workcard_result_string(workcard_result_t result);

// 结束 C ABI 声明区域。
#ifdef __cplusplus
}
#endif

#endif
