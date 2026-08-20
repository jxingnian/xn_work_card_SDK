// 引入当前模块的公开声明。
#include "bluetooth_demo.h"

// 引入标准输入输出和字符串函数。
#include <stdio.h>
#include <string.h>

// 输出一个设备结构，避免客户重复编写字段格式化代码。
static void workcard_bluetooth_demo_print_device(const workcard_bluetooth_device_t *device)
{
    // 忽略空指针，保证扫描结果异常时 Demo 不崩溃。
    if (device == NULL) {
        return;
    }
    // 名称是 UTF-8，直接按 SDK 约定输出。
    printf("蓝牙设备: address=%s name=%s rssi=%d paired=%u connected=%u trusted=%u\n",
           device->address,
           device->name,
           device->rssi_dbm,
           device->paired,
           device->connected,
           device->trusted);
}

// 执行一次状态查询、扫描、可选配对连接和音频路由设置。
int workcard_bluetooth_demo_run(workcard_sdk_t *sdk, const char *address)
{
    // 校验 SDK 句柄，避免把无效参数传到底层服务。
    if (sdk == NULL) {
        return -1;
    }

    // 查询控制器状态，客户可据此决定是否显示蓝牙配置页面。
    workcard_bluetooth_status_t status;
    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    workcard_result_t result = workcard_bluetooth_get_status(sdk, &status);
    if (result != WORKCARD_OK) {
        fprintf(stderr, "蓝牙状态查询失败: %s\n", workcard_result_string(result));
        return -1;
    }
    printf("蓝牙控制器: address=%s name=%s powered=%u connected=%u a2dp=%u\n",
           status.controller_address,
           status.controller_name,
           status.powered,
           status.connected,
           status.a2dp_source_ready);

    // 扫描十秒并读取真实 BlueZ 结果，不把 Controller 行当成设备。
    workcard_bluetooth_device_t devices[WORKCARD_BLUETOOTH_MAX_DEVICES];
    uint32_t device_count = 0U;
    memset(devices, 0, sizeof(devices));
    result = workcard_bluetooth_scan(
        sdk,
        10U,
        devices,
        WORKCARD_BLUETOOTH_MAX_DEVICES,
        &device_count);
    if (result != WORKCARD_OK) {
        fprintf(stderr, "蓝牙扫描失败: %s\n", workcard_result_string(result));
        return -1;
    }
    printf("蓝牙扫描完成: count=%u\n", device_count);
    for (uint32_t index = 0U; index < device_count; ++index) {
        workcard_bluetooth_demo_print_device(&devices[index]);
    }

    // 提供按 MAC 地址配对、信任并连接的完整调用示例。
    if (address != NULL && address[0] != '\0') {
        result = workcard_bluetooth_pair_and_connect(sdk, address);
        if (result != WORKCARD_OK) {
            fprintf(stderr, "蓝牙配对连接失败: %s\n", workcard_result_string(result));
            return -1;
        }
        printf("蓝牙已配对连接: %s\n", address);
    }

    // 设置蓝牙优先输出；不可用时底层 SDK 按正式策略回退内置扬声器。
    result = workcard_audio_set_output_route(sdk, WORKCARD_AUDIO_OUTPUT_BLUETOOTH_PREFERRED);
    if (result != WORKCARD_OK) {
        fprintf(stderr, "蓝牙优先音频路由设置失败: %s\n", workcard_result_string(result));
        return -1;
    }
    return 0;
}
