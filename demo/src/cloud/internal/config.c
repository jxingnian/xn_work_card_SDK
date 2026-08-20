#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 去除字符串左侧空白并返回第一个有效字符。
static char *xn_trim_left(char *text)
{
    // 跳过全部 ASCII 空白字符。
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    // 返回去除左侧空白后的地址。
    return text;
}

// 原地去除字符串右侧空白。
static void xn_trim_right(char *text)
{
    // 获取当前字符串长度。
    size_t text_length = strlen(text);

    // 从末尾向前删除全部 ASCII 空白字符。
    while (text_length > 0 && isspace((unsigned char)text[text_length - 1])) {
        text[text_length - 1] = '\0';
        text_length--;
    }
}

// 安全复制文本配置值并确保结尾存在空字符。
static void xn_copy_text(char *target, size_t target_length, const char *source)
{
    // 空目标缓冲区无法写入任何内容。
    if (target_length == 0) {
        return;
    }

    // 使用格式化复制保证目标缓冲区不会越界。
    snprintf(target, target_length, "%s", source);
}

// 将十进制配置值转换为无符号整数。
static int xn_parse_unsigned(const char *text, unsigned int *value)
{
    // 保存数字转换结束位置。
    char *end_pointer = NULL;

    // 清空上一次标准库错误状态。
    errno = 0;

    // 按十进制读取配置数字。
    unsigned long parsed_value = strtoul(text, &end_pointer, 10);

    // 拒绝空值、非法字符、溢出和超过无符号整数范围的值。
    if (errno != 0 || end_pointer == text || *end_pointer != '\0' || parsed_value > 0xFFFFFFFFUL) {
        return -1;
    }

    // 保存通过校验的配置数字。
    *value = (unsigned int)parsed_value;

    // 返回解析成功。
    return 0;
}

// 加载 MQTT 动态配置写入的原子状态文件。
static int xn_config_load_managed_state(
    xn_app_config *config,
    char *error_message,
    size_t error_message_length)
{
    // 状态文件尚未创建时继续使用交付配置中的初始值。
    FILE *state_file = fopen(config->managed_state_file, "r");
    if (state_file == NULL) {
        return 0;
    }

    // 保存动态状态文件中的完整摄像头参数。
    unsigned int revision = 0U;
    unsigned int width = 0U;
    unsigned int height = 0U;
    unsigned int fps = 0U;
    unsigned int gop = 0U;
    unsigned int bitrate_bps = 0U;
    unsigned int field_count = 0U;
    char line_buffer[256];
    while (fgets(line_buffer, sizeof(line_buffer), state_file) != NULL) {
        // 去除行尾空白并拆分键值。
        xn_trim_right(line_buffer);
        char *line_text = xn_trim_left(line_buffer);
        if (*line_text == '\0' || *line_text == '#') {
            continue;
        }
        char *separator = strchr(line_text, '=');
        if (separator == NULL) {
            fclose(state_file);
            snprintf(error_message, error_message_length, "动态摄像头状态文件格式错误");
            return -1;
        }
        *separator = '\0';
        xn_trim_right(line_text);
        char *value_text = xn_trim_left(separator + 1);
        xn_trim_right(value_text);
        unsigned int *target = NULL;
        if (strcmp(line_text, "revision") == 0) {
            target = &revision;
        } else if (strcmp(line_text, "video_width") == 0) {
            target = &width;
        } else if (strcmp(line_text, "video_height") == 0) {
            target = &height;
        } else if (strcmp(line_text, "video_fps") == 0) {
            target = &fps;
        } else if (strcmp(line_text, "video_gop") == 0) {
            target = &gop;
        } else if (strcmp(line_text, "video_bitrate_bps") == 0) {
            target = &bitrate_bps;
        } else {
            fclose(state_file);
            snprintf(error_message, error_message_length, "动态摄像头状态文件包含未知字段");
            return -1;
        }
        if (xn_parse_unsigned(value_text, target) != 0) {
            fclose(state_file);
            snprintf(error_message, error_message_length, "动态摄像头状态文件数字格式错误");
            return -1;
        }
        field_count++;
    }
    fclose(state_file);

    // 状态文件必须包含完整 revision 和五项摄像头参数。
    if (field_count != 6U || revision == 0U || width == 0U || height == 0U || fps == 0U || gop == 0U || bitrate_bps == 0U) {
        snprintf(error_message, error_message_length, "动态摄像头状态文件字段不完整");
        return -1;
    }
    config->managed_revision = revision;
    config->video_width = width;
    config->video_height = height;
    config->video_fps = fps;
    config->video_gop = gop;
    config->video_bitrate_bps = bitrate_bps;
    return 0;
}

// 写入稳定且低带宽的默认配置。
void xn_config_set_defaults(xn_app_config *config)
{
    // 清空配置结构中的历史数据。
    memset(config, 0, sizeof(*config));

    // 设置生产环境设备 WebSocket 地址。
    xn_copy_text(config->server_url, sizeof(config->server_url), "wss://iot.xingnian.vip/device-stream");

    // 设置随程序部署的 CA 证书包路径。
    // 默认读取本工程安装目录中的生产 CA 证书包。
    xn_copy_text(config->ca_file, sizeof(config->ca_file), "/opt/workcard-demo/ca-certificates.crt");

    // 设置当前生产 MQTT TLS Broker 主机名。
    xn_copy_text(config->mqtt_host, sizeof(config->mqtt_host), "iot.xingnian.vip");

    // 设置当前生产 MQTT TLS 端口。
    config->mqtt_port = 8883U;

    // 设置首个正式设备端版本号。
    xn_copy_text(config->firmware_version, sizeof(config->firmware_version), "V1.0.0");

    // 设置兼容历史配置字段的占位状态文件路径。
    xn_copy_text(
        config->managed_state_file,
        sizeof(config->managed_state_file),
        "/opt/workcard-demo/unused-media-state.json");

    // 设置适合当前设备和公网传输的视频宽度。
    config->video_width = 640;

    // 设置适合后台实时预览的视频高度。
    config->video_height = 320;

    // 设置低带宽实时预览帧率。
    config->video_fps = 5;

    // 设置两秒一个关键帧的编码间隔。
    config->video_gop = 10;

    // 设置低带宽实时预览的默认 H.264 码率。
    config->video_bitrate_bps = 512000;

    // 设置语音场景使用的 Opus 采样率。
    config->audio_sample_rate = 16000;

    // 设置单声道音频采集。
    config->audio_channels = 1;

    // 设置当前核心板的有线备用接口。
    xn_copy_text(config->wired_interface, sizeof(config->wired_interface), "eth0");

    // 设置当前 Air780EGP 网关默认地址，客户可在配置中覆盖。
    xn_copy_text(config->wired_gateway, sizeof(config->wired_gateway), "192.168.10.1");

    // 设置网络切换和 WiFi 关联的最大等待时间。
    config->network_switch_timeout_seconds = 30U;

    // 设置只保存公开 WiFi 元数据的应用层状态文件。
    xn_copy_text(config->wifi_state_file, sizeof(config->wifi_state_file), "/opt/workcard-demo/wifi-state.conf");
}

// 从 UTF-8 键值配置文件读取运行参数。
int xn_config_load(const char *file_path, xn_app_config *config, char *error_message, size_t error_message_length)
{
    // 使用默认值初始化全部可选参数。
    xn_config_set_defaults(config);

    // 以只读方式打开设备配置文件。
    FILE *config_file = fopen(file_path, "r");

    // 配置文件无法打开时返回明确错误。
    if (config_file == NULL) {
        snprintf(error_message, error_message_length, "无法打开配置文件 %s", file_path);
        return -1;
    }

    // 保存逐行读取的配置内容。
    char line_buffer[1024];

    // 保存当前配置文件行号。
    unsigned int line_number = 0;

    // 逐行解析键值配置。
    while (fgets(line_buffer, sizeof(line_buffer), config_file) != NULL) {
        // 更新当前配置文件行号。
        line_number++;

        // 去除当前行右侧换行和空白。
        xn_trim_right(line_buffer);

        // 去除当前行左侧空白。
        char *line_text = xn_trim_left(line_buffer);

        // 跳过空行和注释行。
        if (*line_text == '\0' || *line_text == '#') {
            continue;
        }

        // 查找键值分隔符。
        char *separator = strchr(line_text, '=');

        // 缺少分隔符时拒绝继续使用不完整配置。
        if (separator == NULL) {
            snprintf(error_message, error_message_length, "配置文件第 %u 行缺少等号", line_number);
            fclose(config_file);
            return -1;
        }

        // 将键和值拆分为两个独立字符串。
        *separator = '\0';

        // 去除配置键右侧空白。
        xn_trim_right(line_text);

        // 去除配置值两侧空白。
        char *value_text = xn_trim_left(separator + 1);
        xn_trim_right(value_text);

        // 保存数字配置解析结果。
        int number_result = 0;

        // 根据配置键写入对应字段。
        if (strcmp(line_text, "server_url") == 0) {
            xn_copy_text(config->server_url, sizeof(config->server_url), value_text);
        } else if (strcmp(line_text, "ca_file") == 0) {
            xn_copy_text(config->ca_file, sizeof(config->ca_file), value_text);
        } else if (strcmp(line_text, "device_id") == 0) {
            xn_copy_text(config->device_id, sizeof(config->device_id), value_text);
        } else if (strcmp(line_text, "device_secret") == 0) {
            xn_copy_text(config->device_secret, sizeof(config->device_secret), value_text);
        } else if (strcmp(line_text, "mqtt_host") == 0) {
            xn_copy_text(config->mqtt_host, sizeof(config->mqtt_host), value_text);
        } else if (strcmp(line_text, "mqtt_port") == 0) {
            number_result = xn_parse_unsigned(value_text, &config->mqtt_port);
        } else if (strcmp(line_text, "mqtt_username") == 0) {
            xn_copy_text(config->mqtt_username, sizeof(config->mqtt_username), value_text);
        } else if (strcmp(line_text, "mqtt_password") == 0) {
            xn_copy_text(config->mqtt_password, sizeof(config->mqtt_password), value_text);
        } else if (strcmp(line_text, "firmware_version") == 0) {
            xn_copy_text(config->firmware_version, sizeof(config->firmware_version), value_text);
        } else if (strcmp(line_text, "managed_state_file") == 0) {
            xn_copy_text(config->managed_state_file, sizeof(config->managed_state_file), value_text);
        } else if (strcmp(line_text, "video_width") == 0) {
            number_result = xn_parse_unsigned(value_text, &config->video_width);
        } else if (strcmp(line_text, "video_height") == 0) {
            number_result = xn_parse_unsigned(value_text, &config->video_height);
        } else if (strcmp(line_text, "video_fps") == 0) {
            number_result = xn_parse_unsigned(value_text, &config->video_fps);
        } else if (strcmp(line_text, "video_gop") == 0) {
            number_result = xn_parse_unsigned(value_text, &config->video_gop);
        } else if (strcmp(line_text, "video_bitrate_bps") == 0) {
            number_result = xn_parse_unsigned(value_text, &config->video_bitrate_bps);
        } else if (strcmp(line_text, "audio_sample_rate") == 0) {
            number_result = xn_parse_unsigned(value_text, &config->audio_sample_rate);
        } else if (strcmp(line_text, "audio_channels") == 0) {
            number_result = xn_parse_unsigned(value_text, &config->audio_channels);
        } else if (strcmp(line_text, "wired_interface") == 0) {
            xn_copy_text(config->wired_interface, sizeof(config->wired_interface), value_text);
        } else if (strcmp(line_text, "wired_gateway") == 0) {
            xn_copy_text(config->wired_gateway, sizeof(config->wired_gateway), value_text);
        } else if (strcmp(line_text, "network_switch_timeout_seconds") == 0) {
            number_result = xn_parse_unsigned(value_text, &config->network_switch_timeout_seconds);
        } else if (strcmp(line_text, "wifi_state_file") == 0) {
            xn_copy_text(config->wifi_state_file, sizeof(config->wifi_state_file), value_text);
        } else {
            // 未知配置键通常表示拼写错误，因此直接拒绝启动。
            snprintf(error_message, error_message_length, "配置文件第 %u 行存在未知配置键 %s", line_number, line_text);
            fclose(config_file);
            return -1;
        }

        // 数字配置解析失败时返回准确行号。
        if (number_result != 0) {
            snprintf(error_message, error_message_length, "配置文件第 %u 行数字格式错误", line_number);
            fclose(config_file);
            return -1;
        }
    }

    // 关闭已经读取完成的配置文件。
    fclose(config_file);

    // 在启动媒体链路前覆盖为最后一次经过硬件验证的动态配置。
    if (xn_config_load_managed_state(config, error_message, error_message_length) != 0) {
        return -1;
    }

    // 设备编号和设备密钥是正式连接的必填项。
    if (config->device_id[0] == '\0' || config->device_secret[0] == '\0') {
        snprintf(error_message, error_message_length, "device_id 和 device_secret 不能为空");
        return -1;
    }

    // MQTT 连接参数全部属于 Demo 应用层必填项。
    if (config->mqtt_host[0] == '\0' || config->mqtt_port == 0U || config->mqtt_port > 65535U ||
        config->mqtt_username[0] == '\0' || config->mqtt_password[0] == '\0') {
        snprintf(error_message, error_message_length, "mqtt_host、mqtt_port、mqtt_username 和 mqtt_password 必须完整有效");
        return -1;
    }

    // 有线保活参数必须存在，具体字符和 IPv4 格式由网络切换模块再次严格校验。
    if (config->wired_interface[0] == '\0' || config->wired_gateway[0] == '\0' || config->wifi_state_file[0] == '\0' ||
        config->network_switch_timeout_seconds < 5U || config->network_switch_timeout_seconds > 120U) {
        snprintf(error_message, error_message_length, "wired_interface、wired_gateway、wifi_state_file 必须有效，network_switch_timeout_seconds 必须为 5-120 秒");
        return -1;
    }

    // 限制当前硬件已验证的视频参数范围。
    if (config->video_width < 160U || config->video_width > 1600U || config->video_height < 120U ||
        config->video_height > 1200U || (config->video_width % 2U) != 0U || (config->video_height % 2U) != 0U ||
        config->video_fps < 1U || config->video_fps > 30U || config->video_gop < 1U || config->video_gop > 300U ||
        config->video_bitrate_bps < 64000U || config->video_bitrate_bps > 4000000U) {
        snprintf(error_message, error_message_length, "视频参数超出当前硬件能力范围");
        return -1;
    }

    // 当前正式方案固定使用 16kHz 单声道 Opus。
    if (config->audio_sample_rate != 16000 || config->audio_channels != 1) {
        snprintf(error_message, error_message_length, "当前版本仅支持 16000Hz 单声道 Opus");
        return -1;
    }

    // 返回配置读取成功。
    return 0;
}
