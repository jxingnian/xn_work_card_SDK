#ifndef WORKCARD_BLUETOOTH_H
#define WORKCARD_BLUETOOTH_H

// 引入公共类型和固定文本容量。
#include "workcard_types.h"

// 允许公开头文件同时被 C 和 C++ 客户程序引用。
#ifdef __cplusplus
extern "C" {
#endif

// 前置声明 SDK 不透明句柄。
typedef struct workcard_sdk workcard_sdk_t;

// 定义一次查询最多返回的经典蓝牙设备数量。
#define WORKCARD_BLUETOOTH_MAX_DEVICES 32U

// 保存一个经典蓝牙设备的信息。
typedef struct workcard_bluetooth_device {
    // SDK 填写当前结构体大小。
    uint32_t struct_size;

    // 保存标准蓝牙 MAC 地址。
    char address[WORKCARD_ADDRESS_LENGTH];

    // 保存设备 UTF-8 名称。
    char name[WORKCARD_TEXT_LENGTH];

    // 保存扫描阶段可获得的接收信号强度。
    int32_t rssi_dbm;

    // 非零表示设备已经完成配对。
    uint8_t paired;

    // 非零表示设备当前已经连接。
    uint8_t connected;

    // 非零表示设备已经被 BlueZ 标记为可信。
    uint8_t trusted;

    // 保存用于未来扩展的对齐空间。
    uint8_t reserved;
} workcard_bluetooth_device_t;

// 保存经典蓝牙控制器状态。
typedef struct workcard_bluetooth_status {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 非零表示控制器已经上电。
    uint8_t powered;

    // 非零表示当前存在已连接设备。
    uint8_t connected;

    // 非零表示 A2DP Source 服务已经运行。
    uint8_t a2dp_source_ready;

    // 保存用于未来扩展的对齐空间。
    uint8_t reserved;

    // 保存当前控制器名称。
    char controller_name[WORKCARD_TEXT_LENGTH];

    // 保存当前控制器地址。
    char controller_address[WORKCARD_ADDRESS_LENGTH];
} workcard_bluetooth_status_t;

// 查询经典蓝牙控制器和 A2DP Source 状态。
workcard_result_t workcard_bluetooth_get_status(
    workcard_sdk_t *sdk,
    workcard_bluetooth_status_t *status);

// 打开或关闭经典蓝牙控制器和音频服务。
workcard_result_t workcard_bluetooth_set_power(workcard_sdk_t *sdk, uint8_t enabled);

// 扫描经典蓝牙设备并返回实际写入数量。
workcard_result_t workcard_bluetooth_scan(
    workcard_sdk_t *sdk,
    uint32_t duration_seconds,
    workcard_bluetooth_device_t *devices,
    uint32_t capacity,
    uint32_t *device_count);

// 配对、信任并连接指定蓝牙设备。
workcard_result_t workcard_bluetooth_pair_and_connect(workcard_sdk_t *sdk, const char *address);

// 连接一个已经配对的蓝牙设备。
workcard_result_t workcard_bluetooth_connect(workcard_sdk_t *sdk, const char *address);

// 断开指定蓝牙设备。
workcard_result_t workcard_bluetooth_disconnect(workcard_sdk_t *sdk, const char *address);

// 删除指定蓝牙设备的配对记录。
workcard_result_t workcard_bluetooth_remove_pairing(workcard_sdk_t *sdk, const char *address);

// 查询 BlueZ 当前已知设备并返回配对和连接状态。
workcard_result_t workcard_bluetooth_get_devices(
    workcard_sdk_t *sdk,
    workcard_bluetooth_device_t *devices,
    uint32_t capacity,
    uint32_t *device_count);

// 结束 C ABI 声明区域。
#ifdef __cplusplus
}
#endif

#endif
