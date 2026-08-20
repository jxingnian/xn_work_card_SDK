# 以太网与网络状态 API

```c
workcard_result_t workcard_network_get_status(
    workcard_sdk_t *sdk,
    workcard_network_type_t type,
    workcard_network_status_t *status);
```

`type` 支持：

- `WORKCARD_NETWORK_ETHERNET`：当前固定接口 `eth0`。
- `WORKCARD_NETWORK_WIFI`：当前固定接口 `wlan0`。

返回内容：

- Linux 接口名称。
- 接口是否 UP 且 RUNNING。
- 是否取得 IPv4。
- IPv4 地址。
- 当前接口默认网关。
- `/etc/resolv.conf` 中首个 DNS 地址。

Air780EGP 未来通过网线为核心板提供网络时，应用仍通过以太网状态 API 判断核心板链路和地址；Air780EGP 自身 SIM、信号和注册状态属于后续蜂窝模块 API。
