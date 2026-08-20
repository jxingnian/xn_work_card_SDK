#ifndef XINGNIAN_TIME_UTILS_H
#define XINGNIAN_TIME_UTILS_H

#include <stdint.h>

// 返回单调递增的微秒时间戳，供音视频使用同一个时钟基准。
uint64_t xn_monotonic_time_us(void);

// 返回当前 Unix 毫秒时间戳，供心跳消息使用。
uint64_t xn_unix_time_ms(void);

#endif
