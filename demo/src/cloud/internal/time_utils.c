#include "time_utils.h"

#include <time.h>

// 返回单调递增的微秒时间戳，避免系统时间校准导致媒体时间倒退。
uint64_t xn_monotonic_time_us(void)
{
    // 保存单调时钟结果。
    struct timespec current_time;

    // 读取不受系统时间调整影响的单调时钟。
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    // 将秒和纳秒统一转换为微秒。
    return ((uint64_t)current_time.tv_sec * 1000000ULL) + ((uint64_t)current_time.tv_nsec / 1000ULL);
}

// 返回当前 Unix 毫秒时间戳，供服务器记录设备时间。
uint64_t xn_unix_time_ms(void)
{
    // 保存实时时钟结果。
    struct timespec current_time;

    // 读取当前系统实时时钟。
    clock_gettime(CLOCK_REALTIME, &current_time);

    // 将秒和纳秒统一转换为毫秒。
    return ((uint64_t)current_time.tv_sec * 1000ULL) + ((uint64_t)current_time.tv_nsec / 1000000ULL);
}
