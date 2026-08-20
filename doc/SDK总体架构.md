# SDK 总体架构

## 1. 设计目标

公开 API 不绑定 Hi3516、SC2356、G873/WS73U、RTL8723DS 或未来 Air780EGP 的型号名称。客户应用依赖稳定 C ABI，硬件差异由固定固件中的服务端适配层承担。

```text
客户应用 / 工牌 Demo
        |
        | workcard_* C API
        v
libworkcard_sdk.so
        |
        | Unix Domain Socket 私有协议
        v
workcard-sdk-service
        |
        +-- 摄像头与音频后端
        +-- WiFi 后端
        +-- 经典蓝牙后端
        +-- 以太网状态后端
        +-- GNSS/IMU 平台扩展点
        |
        v
固定 rootfs、驱动、HAL 和硬件
```

当前固定硬件中，WiFi 后端只使用 G873-USA1/WS73U，经典蓝牙后端只使用必联 BL-M8723DS1/RTL8723DS 的 UART1/H5 接口。两个后端是独立模组，不共享蓝牙驱动或启动状态。

## 2. 客户 ABI

客户只能依赖 `include/workcard` 中的头文件和 `libworkcard_sdk.so.1`。公开层遵守以下规则：

- 使用 C11 可调用的 `extern "C"` ABI。
- 不公开 C++ STL、JsonCpp、OpenSSL、HAL 和芯片类型。
- SDK 句柄为不透明指针。
- 公开结构包含 `struct_size`。
- 公开枚举使用稳定数值。
- ABI 主版本不变时保持已有函数和字段语义。
- 新增能力优先通过结构尾部扩展和能力查询发布。

## 3. 服务资源模型

第一版服务一次只接受一个客户应用连接。摄像头、编码器、麦克风、扬声器和无线控制器均属于设备级资源，禁止多个客户进程竞争初始化。

客户断开或崩溃后，服务会停止统一媒体运行时、释放 HAL 资源并重新等待连接。媒体数据经过有界队列发送；客户端处理过慢时服务丢弃旧排队帧，避免阻塞 HAL 编码线程。

## 4. 当前媒体模型

当前硬件使用一个统一 HAL 运行时：

```text
SC2356 -> VI -> VPSS -> VENC -> H.264 回调
麦克风 -> AI -> AENC -> Opus 回调
Opus 下行 -> ADEC -> AO / BlueALSA
```

因此第一版调用 `workcard_camera_start` 时会初始化视频、音频采集和播放链路。音频回调应在启动摄像头前注册，以免丢失首包。

## 5. 多硬件扩展

后续核心板、摄像头、Air780EGP、蓝牙模组和 IMU 通过服务端平台后端扩展。客户必须调用 `workcard_sdk_get_module_capability` 和各模块能力查询，不能假定不同硬件支持相同参数范围。

GNSS 和 IMU 在 1.0.0 中只有稳定数据模型，当前固件返回不支持，不返回零坐标或虚假传感器数据。

## 6. 云端边界

SDK 不包含 MQTT、WSS、设备鉴权、Topic、云端重连或凭据存储。工牌 Demo 的 `demo/src/cloud` 和 `demo/src/mqtt` 是应用层参考实现，继续连接当前 `iot.xingnian.vip`，但不属于 SDK ABI。
