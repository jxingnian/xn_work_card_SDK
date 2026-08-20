# GNSS API

第一版公开稳定定位数据模型，但当前 Hi3516CV610、G873 WiFi 和必联 RTL8723DS 蓝牙固定固件尚未接入 Air780EGP GNSS 串口，因此不提供定位数据。

## 支持状态

```c
workcard_result_t workcard_gnss_is_supported(
    workcard_sdk_t *sdk,
    uint8_t *supported);
```

当前返回成功且 `supported=0`。

## 定位查询

```c
workcard_result_t workcard_gnss_get_location(
    workcard_sdk_t *sdk,
    workcard_gnss_location_t *location);
```

当前返回 `WORKCARD_ERROR_NOT_SUPPORTED`，不会返回零坐标、固定坐标或伪造卫星数量。

未来 Air780EGP 后端将填充 WGS84 经纬度、海拔、速度、方向、卫星数量和 UTC 毫秒时间戳。客户仍必须先查询能力。
