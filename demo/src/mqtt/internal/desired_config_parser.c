// 引入本模块公开的数据结构。
#include "desired_config_parser.h"

// 引入字符判断、格式化和内存函数。
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 保存递归解析的临时上下文。
typedef struct {
    // 保存 JSON 文本起始地址。
    const char *begin;
    // 保存 JSON 文本结束地址。
    const char *end;
    // 保存输出配置。
    workcard_desired_config_t *config;
    // 保存错误文本缓冲区。
    char *error_message;
    // 保存错误文本缓冲区长度。
    size_t error_message_length;
} workcard_json_context_t;

// 写入第一条解析错误并返回失败。
static int workcard_json_error(workcard_json_context_t *context, const char *message)
{
    // 只保留第一条确定错误，避免覆盖根因。
    if (context->error_message != NULL && context->error_message_length > 0U && context->error_message[0] == '\0') {
        (void)snprintf(context->error_message, context->error_message_length, "%s", message);
    }
    return -1;
}

// 跳过 JSON 空白字符。
static void workcard_json_skip_space(const char **cursor, const char *end)
{
    // 只跳过 JSON 标准允许的四类空白。
    while (*cursor < end && (**cursor == ' ' || **cursor == '\t' || **cursor == '\r' || **cursor == '\n')) {
        (*cursor)++;
    }
}

// 解析 JSON 字符串并复制到有限长度缓冲区。
static int workcard_json_parse_string(
    workcard_json_context_t *context,
    const char **cursor,
    char *output,
    size_t output_length)
{
    // 字符串必须以双引号开头。
    if (*cursor >= context->end || **cursor != '"') {
        return workcard_json_error(context, "JSON 字符串格式错误");
    }
    (*cursor)++;

    // 保存当前输出长度并逐字符处理转义。
    size_t written = 0U;
    while (*cursor < context->end && **cursor != '"') {
        unsigned char character = (unsigned char)**cursor;
        (*cursor)++;
        if (character == '\\') {
            if (*cursor >= context->end) {
                return workcard_json_error(context, "JSON 字符串转义不完整");
            }
            character = (unsigned char)**cursor;
            (*cursor)++;
            if (character == '"' || character == '\\' || character == '/' || character == 'b' ||
                character == 'f' || character == 'n' || character == 'r' || character == 't') {
                // 配置字符串只允许协议实际使用的 JSON 单字符转义。
                if (character == 'b' || character == 'f' || character == 'n' || character == 'r' || character == 't') {
                    character = ' ';
                }
            } else {
                return workcard_json_error(context, "JSON 字符串包含不支持的转义");
            }
        } else if (character < 0x20U) {
            return workcard_json_error(context, "JSON 字符串包含控制字符");
        }
        if (output != NULL && output_length > 0U && written + 1U < output_length) {
            output[written++] = (char)character;
        }
    }

    // 字符串必须以闭合双引号结束。
    if (*cursor >= context->end || **cursor != '"') {
        return workcard_json_error(context, "JSON 字符串缺少结束引号");
    }
    (*cursor)++;
    if (output != NULL && output_length > 0U) {
        output[written] = '\0';
    }
    return 0;
}

// 解析无符号整数并限制为 32 位范围。
static int workcard_json_parse_unsigned(workcard_json_context_t *context, const char **cursor, unsigned int *value)
{
    // 读取连续数字文本。
    const char *number_start = *cursor;
    while (*cursor < context->end && isdigit((unsigned char)**cursor) != 0) {
        (*cursor)++;
    }
    if (*cursor == number_start) {
        return workcard_json_error(context, "JSON 数字格式错误");
    }

    // 使用临时缓冲区避免依赖非零结尾的输入。
    char number_text[24];
    const size_t number_length = (size_t)(*cursor - number_start);
    if (number_length >= sizeof(number_text)) {
        return workcard_json_error(context, "JSON 数字超出范围");
    }
    memcpy(number_text, number_start, number_length);
    number_text[number_length] = '\0';
    char *end_pointer = NULL;
    const unsigned long parsed = strtoul(number_text, &end_pointer, 10);
    if (end_pointer == NULL || *end_pointer != '\0' || parsed > 0xffffffffUL) {
        return workcard_json_error(context, "JSON 数字超出范围");
    }
    *value = (unsigned int)parsed;
    return 0;
}

// 跳过任意 JSON 值，同时验证嵌套结构语法。
static int workcard_json_skip_value(workcard_json_context_t *context, const char **cursor);

// 跳过 JSON 数组。
static int workcard_json_skip_array(workcard_json_context_t *context, const char **cursor)
{
    (*cursor)++;
    workcard_json_skip_space(cursor, context->end);
    if (*cursor < context->end && **cursor == ']') {
        (*cursor)++;
        return 0;
    }
    while (*cursor < context->end) {
        if (workcard_json_skip_value(context, cursor) != 0) {
            return -1;
        }
        workcard_json_skip_space(cursor, context->end);
        if (*cursor < context->end && **cursor == ']') {
            (*cursor)++;
            return 0;
        }
        if (*cursor >= context->end || **cursor != ',') {
            return workcard_json_error(context, "JSON 数组缺少逗号");
        }
        (*cursor)++;
        workcard_json_skip_space(cursor, context->end);
    }
    return workcard_json_error(context, "JSON 数组缺少结束括号");
}

// 比较当前 JSON 路径是否为目标字段。
static int workcard_json_path_is(const char *path, const char *expected)
{
    return strcmp(path, expected) == 0;
}

// 拼接有限深度 JSON 对象路径。
static int workcard_json_append_path(char *path, size_t path_length, const char *key)
{
    size_t current_length = strlen(path);
    const size_t key_length = strlen(key);
    if (current_length + (current_length > 0U ? 1U : 0U) + key_length + 1U > path_length) {
        return -1;
    }
    if (current_length > 0U) {
        path[current_length++] = '.';
    }
    memcpy(path + current_length, key, key_length + 1U);
    return 0;
}

// 解析 JSON 对象并提取协议需要的字段。
static int workcard_json_parse_object(workcard_json_context_t *context, const char **cursor, char *path)
{
    (*cursor)++;
    workcard_json_skip_space(cursor, context->end);
    if (*cursor < context->end && **cursor == '}') {
        (*cursor)++;
        return 0;
    }
    while (*cursor < context->end) {
        char key[64];
        memset(key, 0, sizeof(key));
        if (workcard_json_parse_string(context, cursor, key, sizeof(key)) != 0) {
            return -1;
        }
        workcard_json_skip_space(cursor, context->end);
        if (*cursor >= context->end || **cursor != ':') {
            return workcard_json_error(context, "JSON 对象缺少冒号");
        }
        (*cursor)++;
        workcard_json_skip_space(cursor, context->end);
        char child_path[256];
        (void)snprintf(child_path, sizeof(child_path), "%s", path);
        if (workcard_json_append_path(child_path, sizeof(child_path), key) != 0) {
            return workcard_json_error(context, "JSON 配置路径过长");
        }
        if (*cursor < context->end && **cursor == '{') {
            if (workcard_json_parse_object(context, cursor, child_path) != 0) {
                return -1;
            }
        } else if (*cursor < context->end && **cursor == '"') {
            char text_value[96];
            memset(text_value, 0, sizeof(text_value));
            if (workcard_json_parse_string(context, cursor, text_value, sizeof(text_value)) != 0) {
                return -1;
            }
            if (workcard_json_path_is(child_path, "requestId")) {
                (void)snprintf(context->config->request_id, sizeof(context->config->request_id), "%s", text_value);
            }
        } else if (*cursor < context->end && isdigit((unsigned char)**cursor) != 0) {
            unsigned int value = 0U;
            if (workcard_json_parse_unsigned(context, cursor, &value) != 0) {
                return -1;
            }
            if (workcard_json_path_is(child_path, "revision")) {
                context->config->revision = value;
            } else if (workcard_json_path_is(child_path, "baseRevision")) {
                context->config->base_revision = value;
            } else if (workcard_json_path_is(child_path, "config.media.video.width")) {
                context->config->video.width = value;
            } else if (workcard_json_path_is(child_path, "config.media.video.height")) {
                context->config->video.height = value;
            } else if (workcard_json_path_is(child_path, "config.media.video.fps")) {
                context->config->video.fps = value;
            } else if (workcard_json_path_is(child_path, "config.media.video.gop")) {
                context->config->video.gop = value;
            } else if (workcard_json_path_is(child_path, "config.media.video.bitrateBps")) {
                context->config->video.bitrate_bps = value;
            }
        } else if (workcard_json_skip_value(context, cursor) != 0) {
            return -1;
        }
        workcard_json_skip_space(cursor, context->end);
        if (*cursor < context->end && **cursor == '}') {
            (*cursor)++;
            return 0;
        }
        if (*cursor >= context->end || **cursor != ',') {
            return workcard_json_error(context, "JSON 对象缺少逗号");
        }
        (*cursor)++;
        workcard_json_skip_space(cursor, context->end);
    }
    return workcard_json_error(context, "JSON 对象缺少结束括号");
}

// 跳过 JSON 任意值的统一入口。
static int workcard_json_skip_value(workcard_json_context_t *context, const char **cursor)
{
    if (*cursor >= context->end) {
        return workcard_json_error(context, "JSON 值缺失");
    }
    if (**cursor == '"') {
        return workcard_json_parse_string(context, cursor, NULL, 0U);
    }
    if (**cursor == '{') {
        char path[1] = "";
        return workcard_json_parse_object(context, cursor, path);
    }
    if (**cursor == '[') {
        return workcard_json_skip_array(context, cursor);
    }
    if (isdigit((unsigned char)**cursor) != 0) {
        unsigned int ignored = 0U;
        return workcard_json_parse_unsigned(context, cursor, &ignored);
    }
    const char *literal = *cursor;
    while (*cursor < context->end && isalpha((unsigned char)**cursor) != 0) {
        (*cursor)++;
    }
    if (*cursor == literal) {
        return workcard_json_error(context, "JSON 值格式错误");
    }
    return 0;
}

// 解析并严格校验后台下发的摄像头 Desired 配置。
int workcard_desired_config_parse(
    const char *payload,
    size_t payload_length,
    workcard_desired_config_t *config,
    char *error_message,
    size_t error_message_length)
{
    if (payload == NULL || config == NULL || payload_length == 0U) {
        return -1;
    }
    memset(config, 0, sizeof(*config));
    if (error_message != NULL && error_message_length > 0U) {
        error_message[0] = '\0';
    }
    workcard_json_context_t context = {
        payload,
        payload + payload_length,
        config,
        error_message,
        error_message_length,
    };
    const char *cursor = context.begin;
    workcard_json_skip_space(&cursor, context.end);
    char root_path[1] = "";
    if (cursor >= context.end || *cursor != '{' || workcard_json_parse_object(&context, &cursor, root_path) != 0) {
        return -1;
    }
    workcard_json_skip_space(&cursor, context.end);
    if (cursor != context.end) {
        return workcard_json_error(&context, "JSON 对象后存在多余内容");
    }
    if (config->request_id[0] == '\0' || config->revision == 0U || config->video.width == 0U ||
        config->video.height == 0U || config->video.fps == 0U || config->video.gop == 0U ||
        config->video.bitrate_bps == 0U) {
        return workcard_json_error(&context, "Desired 缺少完整 revision 或摄像头参数");
    }
    if (config->video.width < 160U || config->video.width > 1600U || config->video.height < 120U ||
        config->video.height > 1200U || (config->video.width % 2U) != 0U || (config->video.height % 2U) != 0U ||
        config->video.fps < 1U || config->video.fps > 30U || config->video.gop < 1U || config->video.gop > 300U ||
        config->video.bitrate_bps < 64000U || config->video.bitrate_bps > 4000000U) {
        return workcard_json_error(&context, "摄像头参数超出当前硬件能力范围");
    }
    return 0;
}
