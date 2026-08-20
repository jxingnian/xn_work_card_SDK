#ifndef WORKCARD_IMU_H
#define WORKCARD_IMU_H

// 引入公共类型和固定宽度整数。
#include "workcard_types.h"

// 允许公开头文件同时被 C 和 C++ 客户程序引用。
#ifdef __cplusplus
extern "C" {
#endif

// 前置声明 SDK 不透明句柄。
typedef struct workcard_sdk workcard_sdk_t;

// 保存一次通用六轴 IMU 原始采样。
typedef struct workcard_imu_sample {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 保存 X 轴角速度，单位为度每秒。
    float gyroscope_x_dps;

    // 保存 Y 轴角速度，单位为度每秒。
    float gyroscope_y_dps;

    // 保存 Z 轴角速度，单位为度每秒。
    float gyroscope_z_dps;

    // 保存 X 轴加速度，单位为标准重力加速度。
    float acceleration_x_g;

    // 保存 Y 轴加速度，单位为标准重力加速度。
    float acceleration_y_g;

    // 保存 Z 轴加速度，单位为标准重力加速度。
    float acceleration_z_g;

    // 保存设备单调时钟微秒时间戳。
    uint64_t timestamp_us;
} workcard_imu_sample_t;

// 查询 IMU 是否已经由当前固件实现。
workcard_result_t workcard_imu_is_supported(workcard_sdk_t *sdk, uint8_t *supported);

// 读取最近一次 IMU 采样，第一版当前硬件明确返回不支持。
workcard_result_t workcard_imu_read(workcard_sdk_t *sdk, workcard_imu_sample_t *sample);

// 结束 C ABI 声明区域。
#ifdef __cplusplus
}
#endif

#endif
