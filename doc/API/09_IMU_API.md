# IMU API

第一版公开通用六轴数据模型，但当前固定固件没有确定型号的 IMU 后端。

## 支持状态

```c
workcard_result_t workcard_imu_is_supported(
    workcard_sdk_t *sdk,
    uint8_t *supported);
```

当前返回成功且 `supported=0`。

## 原始采样

```c
workcard_result_t workcard_imu_read(
    workcard_sdk_t *sdk,
    workcard_imu_sample_t *sample);
```

当前返回 `WORKCARD_ERROR_NOT_SUPPORTED`，不会返回全零伪数据。

未来实现将提供三轴角速度、三轴加速度和单调时钟时间戳。跌倒检测和姿态解算属于需要单独冻结指标的算法能力，不包含在当前 API 中。
