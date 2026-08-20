cmake_minimum_required(VERSION 3.14)

# 声明客户工牌参考应用。
project(workcard_demo C)

# 固定使用稳定 C11 标准。
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 解析 SDK 根目录。
get_filename_component(WORKCARD_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# 定义模块化 Demo 源文件。
add_executable(workcard-demo
    src/main.c
    src/bluetooth/bluetooth_demo.c
    src/cloud/cloud_demo.c
    src/cloud/internal/config.c
    src/cloud/internal/media_packet.c
    src/cloud/internal/time_utils.c
    src/cloud/internal/websocket_client.c
    src/mqtt/mqtt_demo.c
    src/mqtt/internal/wifi_command_parser.c
    src/network/wifi_profile_store.c
    src/network/wifi_text_codec.c
    src/network/network_failover.c
    src/mqtt/internal/desired_config_parser.c
    src/mqtt/internal/mqtt_tls_platform.c
    third_party/paho-embedded-c/MQTTClient-C/src/MQTTClient.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTConnectClient.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTDeserializePublish.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTFormat.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTPacket.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTSerializePublish.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTSubscribeClient.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTUnsubscribeClient.c)

# 配置 SDK、TLS 和 Demo 私有头文件路径。
target_include_directories(workcard-demo PRIVATE
    "${WORKCARD_SDK_ROOT}/include"
    "${WORKCARD_SDK_ROOT}/runtime/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cloud"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cloud/internal")
target_include_directories(workcard-demo PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt/internal"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/paho-embedded-c/MQTTClient-C/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/paho-embedded-c/MQTTPacket/src")

# 启用正式优化、告警和 POSIX 接口声明。
target_compile_options(workcard-demo PRIVATE -O2 -Wall -Wextra -Werror)
target_compile_definitions(workcard-demo PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L MQTTCLIENT_PLATFORM_HEADER=mqtt_tls_platform.h)
target_compile_options(workcard-demo PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt/internal/mqtt_tls_platform.h")

# 未修改的 Paho 上游 C 源码保留其原始类型签名告警，Demo 自有源码仍全部使用 -Werror。
set_source_files_properties(
    third_party/paho-embedded-c/MQTTClient-C/src/MQTTClient.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTConnectClient.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTDeserializePublish.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTFormat.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTPacket.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTSerializePublish.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTSubscribeClient.c
    third_party/paho-embedded-c/MQTTPacket/src/MQTTUnsubscribeClient.c
    PROPERTIES COMPILE_OPTIONS "-Wno-error")

# 只从 SDK 交付目录查找客户库和 TLS 运行库。
target_link_directories(workcard-demo PRIVATE
    "${WORKCARD_SDK_ROOT}/lib/arm-linux-musleabi"
    "${WORKCARD_SDK_ROOT}/runtime/lib")

# 链接客户 SDK、当前生产 TLS 和系统线程库。
target_link_libraries(workcard-demo PRIVATE
    workcard_sdk
    ssl
    crypto
    pthread
    dl
    m)
