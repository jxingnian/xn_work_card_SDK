# WiFi API

## 状态查询

```c
workcard_result_t workcard_wifi_get_status(
    workcard_sdk_t *sdk,
    workcard_wifi_status_t *status);
```

返回关联状态、SSID、BSSID、IPv4 和 RSSI，不返回密码或派生 PSK。

## 扫描

```c
workcard_result_t workcard_wifi_scan(
    workcard_sdk_t *sdk,
    workcard_wifi_scan_result_t *results,
    uint32_t capacity,
    uint32_t *result_count);
```

`capacity` 最大为 32。扫描为同步操作，通常需要约 2 秒，应在业务工作线程调用。结果数组不足时返回实际写入数量，不越界写入。

## 保存并连接 WPA2 网络

```c
workcard_result_t workcard_wifi_connect(
    workcard_sdk_t *sdk,
    const workcard_wifi_config_t *config);
```

- SSID 长度为 1 到 32 字节。
- WPA2 密码长度为 8 到 63 字节。
- 优先级范围为 0 到 100。
- 最多保存 8 个网络。

SDK 使用 PBKDF2-HMAC-SHA1 派生 PSK，持久化文件不保存明文密码。函数成功表示配置已经原子保存并通知 `wpa_supplicant`，不表示 DHCP 已经完成；客户应继续查询状态。

## 断开

```c
workcard_result_t workcard_wifi_disconnect(workcard_sdk_t *sdk);
```

断开当前关联但保留已保存网络。联网守护可能根据产品策略重新关联，因此生产应用应结合固定固件的网络管理策略使用。

## 删除网络

```c
workcard_result_t workcard_wifi_remove_network(
    workcard_sdk_t *sdk,
    const char *ssid);
```

按完整 SSID 删除持久化网络并重载配置。删除不存在的 SSID 视为幂等成功。
