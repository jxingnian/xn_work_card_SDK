// 引入 WiFi 配置清单持久化接口。
#include "wifi_profile_store.h"

// 引入原子文件替换和文本处理接口。
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// 保存状态文件中不含密码的一条 WiFi 元数据。
typedef struct {
    // 保存后台生成的稳定 profile 标识。
    char profile_id[96];
    // 保存 UTF-8 SSID。
    char ssid[128];
    // 保存后台连接优先级。
    unsigned int priority;
    // 保存是否允许管理员连接。
    int enabled;
} workcard_wifi_stored_profile_t;

// 保存状态文件中的完整公开配置快照。
typedef struct {
    // 保存最近一次成功同步的 revision。
    unsigned int revision;
    // 保存状态文件中的 profile 数量。
    unsigned int profile_count;
    // 保存有限 profile 数组。
    workcard_wifi_stored_profile_t profiles[WORKCARD_WIFI_COMMAND_MAX_PROFILES];
} workcard_wifi_stored_state_t;

// 写入模块统一错误文本。
static int workcard_wifi_store_error(char *error, size_t error_length, const char *message)
{
    // 调用方未提供错误缓冲时仍返回失败。
    if (error != NULL && error_length > 0U) (void)snprintf(error, error_length, "%s", message);
    // 返回统一失败值。
    return -1;
}

// 去除状态文件一行末尾的换行字符。
static void workcard_wifi_store_trim_line(char *line)
{
    // 逐个移除 CR 和 LF。
    size_t length = strlen(line);
    while (length > 0U && (line[length - 1U] == '\n' || line[length - 1U] == '\r')) line[--length] = '\0';
}

// 解析一条使用制表符分隔且不含敏感信息的 profile 记录。
static int workcard_wifi_store_parse_profile(char *line, workcard_wifi_stored_profile_t *profile)
{
    // 依次定位四个固定字段。
    char *profile_id = line;
    char *ssid = strchr(profile_id, '\t');
    if (ssid == NULL) return -1;
    *ssid++ = '\0';
    char *priority_text = strchr(ssid, '\t');
    if (priority_text == NULL) return -1;
    *priority_text++ = '\0';
    char *enabled_text = strchr(priority_text, '\t');
    if (enabled_text == NULL) return -1;
    *enabled_text++ = '\0';
    // 拒绝空标识、空 SSID 和额外分隔字段。
    if (profile_id[0] == '\0' || ssid[0] == '\0' || strchr(enabled_text, '\t') != NULL) return -1;
    // 使用严格扫描读取有限数字值。
    unsigned int priority = 0U;
    unsigned int enabled = 0U;
    char trailing = '\0';
    if (sscanf(priority_text, "%u%c", &priority, &trailing) != 1 ||
        sscanf(enabled_text, "%u%c", &enabled, &trailing) != 1 || priority > 100U || enabled > 1U) return -1;
    // 固定缓冲复制前检查 UTF-8 字节长度。
    if (strlen(profile_id) >= sizeof(profile->profile_id) || strlen(ssid) >= sizeof(profile->ssid)) return -1;
    (void)snprintf(profile->profile_id, sizeof(profile->profile_id), "%s", profile_id);
    (void)snprintf(profile->ssid, sizeof(profile->ssid), "%s", ssid);
    profile->priority = priority;
    profile->enabled = (int)enabled;
    // 返回解析成功。
    return 0;
}

// 读取上次同步成功的公开 WiFi 配置状态。
static int workcard_wifi_store_load(const char *state_path, workcard_wifi_stored_state_t *state)
{
    // 文件不存在表示设备尚未接收过 WiFi 配置。
    memset(state, 0, sizeof(*state));
    FILE *file = fopen(state_path, "r");
    if (file == NULL) return 0;
    // 第一行必须是唯一 revision 字段。
    char line[384];
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return -1;
    }
    workcard_wifi_store_trim_line(line);
    char trailing = '\0';
    if (sscanf(line, "revision=%u%c", &state->revision, &trailing) != 1) {
        fclose(file);
        return -1;
    }
    // 后续每行保存一条不含密码的 profile。
    while (fgets(line, sizeof(line), file) != NULL) {
        workcard_wifi_store_trim_line(line);
        if (line[0] == '\0') continue;
        if (state->profile_count >= WORKCARD_WIFI_COMMAND_MAX_PROFILES || strncmp(line, "profile=", 8U) != 0 ||
            workcard_wifi_store_parse_profile(line + 8U, &state->profiles[state->profile_count]) != 0) {
            fclose(file);
            return -1;
        }
        state->profile_count++;
    }
    // 关闭并返回读取结果。
    return fclose(file) == 0 ? 0 : -1;
}

// 将最新公开 WiFi 配置原子保存为仅所有者可读写的文件。
static int workcard_wifi_store_save(const char *state_path, const workcard_wifi_profiles_command_t *command)
{
    // 临时文件必须与正式文件位于同一目录以保证 rename 原子性。
    char temporary_path[384];
    const int path_length = snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", state_path);
    if (path_length <= 0 || (size_t)path_length >= sizeof(temporary_path)) return -1;
    FILE *file = fopen(temporary_path, "w");
    if (file == NULL) return -1;
    // 保存 revision 后逐条保存不含密码的元数据。
    int failed = fprintf(file, "revision=%u\n", command->revision) <= 0;
    for (unsigned int index = 0U; index < command->profile_count && !failed; ++index) {
        const workcard_wifi_command_profile_t *profile = &command->profiles[index];
        failed = fprintf(file, "profile=%s\t%s\t%u\t%d\n", profile->profile_id, profile->ssid, profile->priority, profile->enabled) <= 0;
    }
    // 刷盘完成后再关闭文件，禁止落下半份配置。
    if (!failed) failed = fflush(file) != 0 || fsync(fileno(file)) != 0;
    if (fclose(file) != 0) failed = 1;
    if (failed || chmod(temporary_path, 0600) != 0 || rename(temporary_path, state_path) != 0) {
        (void)unlink(temporary_path);
        return -1;
    }
    // 返回原子保存成功。
    return 0;
}

// 判断旧 profile 是否仍以相同 SSID 启用。
static int workcard_wifi_store_is_retained(const workcard_wifi_stored_profile_t *old_profile, const workcard_wifi_profiles_command_t *command)
{
    // profileId 相同但 SSID 变化时必须删除旧 SSID。
    for (unsigned int index = 0U; index < command->profile_count; ++index) {
        const workcard_wifi_command_profile_t *profile = &command->profiles[index];
        if (strcmp(profile->profile_id, old_profile->profile_id) == 0) return profile->enabled != 0 && strcmp(profile->ssid, old_profile->ssid) == 0;
    }
    // 后台已删除该 profile。
    return 0;
}

// 同步完整配置清单但不主动连接任何 WiFi。
int workcard_wifi_profile_store_apply(workcard_sdk_t *sdk, const char *state_path, const workcard_wifi_profiles_command_t *command, char *error, size_t error_length)
{
    // 检查模块依赖参数。
    if (sdk == NULL || state_path == NULL || state_path[0] == '\0' || command == NULL) return workcard_wifi_store_error(error, error_length, "WiFi 配置状态参数无效");
    // 状态损坏时拒绝猜测要删除的网络。
    workcard_wifi_stored_state_t previous;
    if (workcard_wifi_store_load(state_path, &previous) != 0) return workcard_wifi_store_error(error, error_length, "WiFi 配置状态文件损坏");
    // 删除后台已经取消、禁用或改名的旧网络。
    for (unsigned int index = 0U; index < previous.profile_count; ++index) {
        if (!workcard_wifi_store_is_retained(&previous.profiles[index], command) &&
            workcard_wifi_remove_network(sdk, previous.profiles[index].ssid) != WORKCARD_OK) {
            return workcard_wifi_store_error(error, error_length, "删除已取消的 WiFi 失败");
        }
    }
    // 所有删除完成后才推进本地 revision。
    if (workcard_wifi_store_save(state_path, command) != 0) return workcard_wifi_store_error(error, error_length, "保存 WiFi 配置状态失败");
    // 保存动作本身不连接，连接只能由独立 action 命令触发。
    return 0;
}
