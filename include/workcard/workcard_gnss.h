#ifndef WORKCARD_GNSS_H
#define WORKCARD_GNSS_H

// 引入公共类型和固定宽度整数。
#include "workcard_types.h"

// 允许公开头文件同时被 C 和 C++ 客户程序引用。
#ifdef __cplusplus
extern "C" {
#endif

// 前置声明 SDK 不透明句柄。
typedef struct workcard_sdk workcard_sdk_t;

// 保存一次通用 GNSS 定位结果。
typedef struct workcard_gnss_location {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 非零表示经纬度已经通过模组定位校验。
    uint8_t valid;

    // 保存用于未来扩展的对齐空间。
    uint8_t reserved[3];

    // 保存 WGS84 纬度，北纬为正。
    double latitude;

    // 保存 WGS84 经度，东经为正。
    double longitude;

    // 保存海拔高度，单位为米。
    double altitude_meters;

    // 保存地面速度，单位为米每秒。
    double speed_meters_per_second;

    // 保存相对真北方向角，单位为度。
    double course_degrees;

    // 保存参与定位的卫星数量。
    uint32_t satellites;

    // 保存 UTC Unix 时间戳，单位为毫秒。
    uint64_t utc_time_ms;
} workcard_gnss_location_t;

// 查询 GNSS 是否已经由当前固件实现。
workcard_result_t workcard_gnss_is_supported(workcard_sdk_t *sdk, uint8_t *supported);

// 获取最近一次有效定位，第一版当前硬件明确返回不支持。
workcard_result_t workcard_gnss_get_location(workcard_sdk_t *sdk, workcard_gnss_location_t *location);

// 结束 C ABI 声明区域。
#ifdef __cplusplus
}
#endif

#endif
