# SDK 初始化与生命周期 API

## 适用头文件

```c
#include <workcard/workcard_sdk.h>
```

## 生命周期

```text
workcard_sdk_create
    -> workcard_sdk_start
        -> 业务 API
        -> workcard_camera_stop
    -> workcard_sdk_stop
-> workcard_sdk_destroy
```

第一版服务只允许一个客户应用独占连接。第二个应用无法取得硬件控制权。

## workcard_sdk_create

```c
workcard_result_t workcard_sdk_create(
    const workcard_sdk_config_t *config,
    workcard_sdk_t **sdk);
```

- `config`：创建参数。允许为空，空值使用 `/run/workcard-sdk/service.sock` 和 45 秒请求超时。
- `config->struct_size`：必须填写 `sizeof(workcard_sdk_config_t)`。
- `config->event_callback`：可选通用事件回调。
- `sdk`：成功后返回不透明句柄。
- 本函数只分配客户进程资源，不连接硬件服务。

## workcard_sdk_start

```c
workcard_result_t workcard_sdk_start(workcard_sdk_t *sdk);
```

连接固定 rootfs 中的底层服务并启动接收线程。服务未启动时返回 `WORKCARD_ERROR_NOT_CONNECTED`；重复启动返回 `WORKCARD_ERROR_ALREADY_RUNNING`。

## workcard_sdk_stop

```c
void workcard_sdk_stop(workcard_sdk_t *sdk);
```

关闭服务连接并等待回调线程退出。允许安全重复调用。调用前应先停止摄像头和应用自己的云端线程。

## workcard_sdk_destroy

```c
void workcard_sdk_destroy(workcard_sdk_t *sdk);
```

内部会先执行停止，再释放句柄。返回后客户不得继续访问该指针。

## 回调规则

- 通用事件、H.264 和 Opus 回调均运行在 SDK 接收线程。
- 回调内禁止等待其他线程长期完成、扫描蓝牙或执行耗时网络请求。
- 回调内不要调用可能等待服务响应的同步 SDK API。
- 媒体指针只在当前回调执行期间有效。
