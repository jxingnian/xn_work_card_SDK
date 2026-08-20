#ifndef WORKCARD_TYPES_H
#define WORKCARD_TYPES_H

// 引入固定宽度整数类型，确保客户程序与服务端使用一致的二进制布局。
#include <stdint.h>

// 允许公开头文件同时被 C 和 C++ 客户程序引用。
#ifdef __cplusplus
extern "C" {
#endif

// 定义公开文本字段的固定容量，避免跨编译器传递动态字符串对象。
#define WORKCARD_TEXT_LENGTH 128U

// 定义设备地址字段容量，可容纳标准蓝牙 MAC 地址和结尾空字符。
#define WORKCARD_ADDRESS_LENGTH 32U

// 定义公开 ABI 的主版本，主版本变化表示存在不兼容修改。
#define WORKCARD_ABI_VERSION_MAJOR 1U

// 定义公开 ABI 的次版本，次版本只增加向后兼容能力。
#define WORKCARD_ABI_VERSION_MINOR 0U

// 定义 SDK 正式版本文本。
#define WORKCARD_SDK_VERSION "1.0.0"

// 定义所有公开接口共用的返回码。
typedef enum workcard_result {
    // 表示操作成功完成。
    WORKCARD_OK = 0,

    // 表示调用参数为空、越界或结构长度不正确。
    WORKCARD_ERROR_INVALID_ARGUMENT = -1,

    // 表示当前 SDK 句柄尚未连接到底层服务。
    WORKCARD_ERROR_NOT_CONNECTED = -2,

    // 表示当前硬件或固件不支持请求的能力。
    WORKCARD_ERROR_NOT_SUPPORTED = -3,

    // 表示目标资源已经启动或正在执行相同操作。
    WORKCARD_ERROR_ALREADY_RUNNING = -4,

    // 表示目标资源尚未启动。
    WORKCARD_ERROR_NOT_RUNNING = -5,

    // 表示底层硬件或系统操作执行失败。
    WORKCARD_ERROR_HARDWARE = -6,

    // 表示与底层服务之间的 IPC 通信失败。
    WORKCARD_ERROR_IPC = -7,

    // 表示等待底层服务响应超过限定时间。
    WORKCARD_ERROR_TIMEOUT = -8,

    // 表示当前硬件资源正在被其他客户端占用。
    WORKCARD_ERROR_BUSY = -9,

    // 表示 SDK 或服务端无法分配所需内存。
    WORKCARD_ERROR_NO_MEMORY = -10,

    // 表示配置文件内容或持久化状态不合法。
    WORKCARD_ERROR_CONFIG = -11,

    // 表示服务端返回了当前 SDK 无法识别的协议数据。
    WORKCARD_ERROR_PROTOCOL = -12,

    // 表示发生了未归类的内部错误。
    WORKCARD_ERROR_INTERNAL = -13
} workcard_result_t;

// 定义 SDK 对外报告的功能模块编号。
typedef enum workcard_module {
    // 表示设备基础信息模块。
    WORKCARD_MODULE_DEVICE = 1,

    // 表示摄像头和视频编码模块。
    WORKCARD_MODULE_CAMERA = 2,

    // 表示音频采集和播放模块。
    WORKCARD_MODULE_AUDIO = 3,

    // 表示 WiFi 管理模块。
    WORKCARD_MODULE_WIFI = 4,

    // 表示经典蓝牙模块。
    WORKCARD_MODULE_BLUETOOTH = 5,

    // 表示以太网状态模块。
    WORKCARD_MODULE_ETHERNET = 6,

    // 表示蜂窝模组管理模块。
    WORKCARD_MODULE_CELLULAR = 7,

    // 表示 GNSS 定位模块。
    WORKCARD_MODULE_GNSS = 8,

    // 表示惯性传感器模块。
    WORKCARD_MODULE_IMU = 9
} workcard_module_t;

// 定义通用异步事件类型。
typedef enum workcard_event_type {
    // 表示底层服务连接已经建立。
    WORKCARD_EVENT_SERVICE_CONNECTED = 1,

    // 表示底层服务连接已经断开。
    WORKCARD_EVENT_SERVICE_DISCONNECTED = 2,

    // 表示模块运行状态发生变化。
    WORKCARD_EVENT_MODULE_STATE_CHANGED = 3,

    // 表示模块发生需要客户记录的错误。
    WORKCARD_EVENT_MODULE_ERROR = 4
} workcard_event_type_t;

// 保存一个模块在当前固件中的能力状态。
typedef struct workcard_module_capability {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 保存对应的功能模块编号。
    workcard_module_t module;

    // 非零表示当前固件已经实现并允许调用该模块。
    uint8_t supported;

    // 保存用于未来扩展的对齐空间，调用方必须清零。
    uint8_t reserved[3];
} workcard_module_capability_t;

// 保存设备和当前软件版本信息。
typedef struct workcard_device_info {
    // 调用方必须填写当前结构体大小。
    uint32_t struct_size;

    // 保存客户可识别的产品型号。
    char device_model[WORKCARD_TEXT_LENGTH];

    // 保存硬件版本文本。
    char hardware_revision[WORKCARD_TEXT_LENGTH];

    // 保存固定固件版本文本。
    char firmware_version[WORKCARD_TEXT_LENGTH];

    // 保存当前 SDK 版本文本。
    char sdk_version[WORKCARD_TEXT_LENGTH];

    // 保存公开 ABI 主版本。
    uint32_t abi_version_major;

    // 保存公开 ABI 次版本。
    uint32_t abi_version_minor;
} workcard_device_info_t;

// 保存一个通用 SDK 事件。
typedef struct workcard_event {
    // 调用方和 SDK 使用该字段识别结构体版本。
    uint32_t struct_size;

    // 保存事件类型。
    workcard_event_type_t type;

    // 保存产生事件的模块。
    workcard_module_t module;

    // 保存事件对应的结果码。
    workcard_result_t result;

    // 保存便于现场诊断且不包含敏感凭据的 UTF-8 文本。
    char message[WORKCARD_TEXT_LENGTH];
} workcard_event_t;

// 定义通用事件回调，事件结构只在回调执行期间有效。
typedef void (*workcard_event_callback_t)(const workcard_event_t *event, void *user_data);

// 结束 C ABI 声明区域。
#ifdef __cplusplus
}
#endif

#endif
