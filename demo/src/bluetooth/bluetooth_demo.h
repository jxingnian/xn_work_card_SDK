#ifndef WORKCARD_DEMO_BLUETOOTH_DEMO_H
#define WORKCARD_DEMO_BLUETOOTH_DEMO_H

// 引入公开 SDK 句柄类型。
#include <workcard/workcard_sdk.h>

// 执行一次可重复的经典蓝牙状态、扫描和可选连接示例。
int workcard_bluetooth_demo_run(workcard_sdk_t *sdk, const char *address);

#endif
