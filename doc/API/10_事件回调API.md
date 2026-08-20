# 事件与媒体回调规则

## 通用事件

通过 `workcard_sdk_config_t.event_callback` 注册：

```c
typedef void (*workcard_event_callback_t)(
    const workcard_event_t *event,
    void *user_data);
```

第一版报告服务连接和断开事件。事件消息为 UTF-8 诊断文本，不包含 WiFi 密码、设备密钥或 MQTT 密码。

## 线程模型

通用事件、H.264 和 Opus 回调运行在同一个 SDK 接收线程。客户必须遵守：

1. 回调快速返回。
2. 需要异步处理时复制媒体数据到客户自己的有界队列。
3. 不在回调中调用 WiFi/蓝牙扫描等同步 API。
4. 不保存回调结构或数据指针供回调结束后直接访问。
5. 客户队列满时应有明确的丢帧策略，不能无限分配内存。

## 断线

服务连接断开后同步 API 返回 `WORKCARD_ERROR_NOT_CONNECTED` 或 `WORKCARD_ERROR_IPC`。客户应停止自己的云端发送，销毁旧 SDK 句柄并按业务策略重新创建连接。
