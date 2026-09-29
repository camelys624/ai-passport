// main/codeloom_protocol.c — 见 codeloom_protocol.h。
#include "codeloom_protocol.h"

#include <string.h>

#include "cJSON.h"
#include "codeloom_text.h"
#include "codeloom_url.h"

// 在交给 cJSON 之前扫描嵌套深度，避免恶意输入让递归下降解析器耗尽任务栈。
static bool depth_within_limit(const char *json, size_t length)
{
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (size_t i = 0; i < length; ++i) {
        char c = json[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            if (++depth > CL_JSON_MAX_DEPTH) {
                return false;
            }
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return true;
}

static cl_parse_result_t parse_root(const char *json, size_t length, cJSON **root)
{
    const char *end = NULL;

    *root = NULL;
    if (json == NULL || length == 0) {
        return CL_PARSE_SYNTAX;
    }
    if (length > CL_HTTP_RESPONSE_CAP) {
        return CL_PARSE_TOO_LARGE;
    }
    if (!depth_within_limit(json, length)) {
        return CL_PARSE_TOO_DEEP;
    }
    *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (*root == NULL) {
        return CL_PARSE_SYNTAX;
    }
    // 只允许 JSON 值之后跟空白（或 NUL），拒绝拼接的第二个文档。
    for (const char *p = end; p != NULL && p < json + length; ++p) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '\0') {
            cJSON_Delete(*root);
            *root = NULL;
            return CL_PARSE_SYNTAX;
        }
    }
    if (!cJSON_IsObject(*root)) {
        cJSON_Delete(*root);
        *root = NULL;
        return CL_PARSE_SCHEMA;
    }
    return CL_PARSE_OK;
}

static const cJSON *get(const cJSON *object, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

static bool get_count(const cJSON *object, const char *key, uint32_t *value)
{
    const cJSON *item = get(object, key);
    double number;

    if (!cJSON_IsNumber(item)) {
        return false;
    }
    number = item->valuedouble;
    if (number < 0 || number > 4294967295.0 || number != (double)(uint32_t)number) {
        return false;
    }
    *value = (uint32_t)number;
    return true;
}

// 可选字符串：缺失或 null 时输出空串并返回 true；存在但不是字符串时返回 false。
static bool copy_optional_string(const cJSON *object, const char *key, char *dst,
                                 size_t dst_size, bool *truncated)
{
    const cJSON *item = get(object, key);

    if (item == NULL || cJSON_IsNull(item)) {
        dst[0] = '\0';
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    if (cl_utf8_copy_sanitized(dst, dst_size, item->valuestring, strlen(item->valuestring))) {
        *truncated = true;
    }
    return true;
}

static bool copy_required_string(const cJSON *object, const char *key, char *dst,
                                 size_t dst_size, bool *truncated)
{
    const cJSON *item = get(object, key);

    return cJSON_IsString(item) && item->valuestring != NULL &&
           copy_optional_string(object, key, dst, dst_size, truncated);
}

static bool copy_id(const cJSON *object, const char *key, char *dst, size_t dst_size)
{
    const cJSON *item = get(object, key);

    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        !cl_id_is_safe(item->valuestring) || strlen(item->valuestring) >= dst_size) {
        return false;
    }
    strcpy(dst, item->valuestring);
    return true;
}

typedef struct {
    const char *name;
    int value;
} enum_entry_t;

static int lookup_enum(const char *text, const enum_entry_t *table, size_t count, int fallback)
{
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(text, table[i].name) == 0) {
            return table[i].value;
        }
    }
    return fallback;
}

static const enum_entry_t KIND_NAMES[] = {
    {"tool", CL_KIND_TOOL},       {"file_write", CL_KIND_FILE_WRITE},
    {"shell", CL_KIND_SHELL},     {"network", CL_KIND_NETWORK},
    {"other", CL_KIND_OTHER},
};

static const enum_entry_t TASK_STATUS_NAMES[] = {
    {"backlog", CL_TASK_BACKLOG},         {"todo", CL_TASK_TODO},
    {"in_progress", CL_TASK_IN_PROGRESS}, {"needs_review", CL_TASK_NEEDS_REVIEW},
    {"done", CL_TASK_DONE},               {"canceled", CL_TASK_CANCELED},
};

static const enum_entry_t RUN_STATUS_NAMES[] = {
    {"pending", CL_RUN_PENDING},       {"active", CL_RUN_ACTIVE},
    {"idle", CL_RUN_IDLE},             {"waiting_approval", CL_RUN_WAITING_APPROVAL},
    {"completed", CL_RUN_COMPLETED},   {"failed", CL_RUN_FAILED},
    {"canceled", CL_RUN_CANCELED},     {"lost", CL_RUN_LOST},
};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static bool parse_approval(const cJSON *item, cl_approval_t *out, bool *truncated)
{
    const cJSON *kind;
    const cJSON *created;

    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(item) || !copy_id(item, "id", out->id, sizeof(out->id))) {
        return false;
    }
    kind = get(item, "kind");
    if (!cJSON_IsString(kind) || kind->valuestring == NULL) {
        return false;
    }
    out->kind = (cl_kind_t)lookup_enum(kind->valuestring, KIND_NAMES, COUNT_OF(KIND_NAMES),
                                       CL_KIND_OTHER);
    if (!copy_required_string(item, "title", out->title, sizeof(out->title), truncated) ||
        !copy_optional_string(item, "detail", out->detail, sizeof(out->detail), truncated) ||
        !copy_optional_string(item, "taskTitle", out->task_title, sizeof(out->task_title),
                              truncated)) {
        return false;
    }
    created = get(item, "createdAt");
    if (cJSON_IsString(created) && created->valuestring != NULL) {
        out->created_valid = cl_parse_iso8601(created->valuestring, &out->created_at_s);
    }
    return true;
}

static bool parse_task(const cJSON *item, cl_task_t *out, bool *truncated)
{
    const cJSON *status;
    const cJSON *run;

    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(item) || !copy_id(item, "id", out->id, sizeof(out->id)) ||
        !copy_required_string(item, "title", out->title, sizeof(out->title), truncated)) {
        return false;
    }
    status = get(item, "status");
    if (!cJSON_IsString(status) || status->valuestring == NULL) {
        return false;
    }
    out->status = (cl_task_status_t)lookup_enum(status->valuestring, TASK_STATUS_NAMES,
                                                COUNT_OF(TASK_STATUS_NAMES), CL_TASK_UNKNOWN);
    run = get(item, "runStatus");
    if (run == NULL || cJSON_IsNull(run)) {
        out->run_status = CL_RUN_NONE;
    } else if (cJSON_IsString(run) && run->valuestring != NULL) {
        out->run_status = (cl_run_status_t)lookup_enum(run->valuestring, RUN_STATUS_NAMES,
                                                       COUNT_OF(RUN_STATUS_NAMES), CL_RUN_UNKNOWN);
    } else {
        return false;
    }
    return true;
}

cl_parse_result_t cl_parse_overview(const char *json, size_t length, cl_overview_t *out,
                                    cl_parse_stats_t *stats)
{
    cJSON *root;
    const cJSON *approvals;
    const cJSON *tasks;
    const cJSON *item;
    const cJSON *server_time;
    cl_parse_stats_t local_stats = {0};
    cl_parse_result_t result;

    if (out == NULL) {
        return CL_PARSE_SCHEMA;
    }
    memset(out, 0, sizeof(*out));
    result = parse_root(json, length, &root);
    if (result != CL_PARSE_OK) {
        return result;
    }
    approvals = get(root, "approvals");
    tasks = get(root, "tasks");
    if (!cJSON_IsArray(approvals) || !cJSON_IsArray(tasks) ||
        !get_count(root, "approvalsTotal", &out->approvals_total) ||
        !get_count(root, "tasksTotal", &out->tasks_total) ||
        !copy_optional_string(root, "workspaceName", out->workspace_name,
                              sizeof(out->workspace_name), &local_stats.truncated_strings)) {
        cJSON_Delete(root);
        memset(out, 0, sizeof(*out));
        return CL_PARSE_SCHEMA;
    }
    server_time = get(root, "serverTime");
    if (cJSON_IsString(server_time) && server_time->valuestring != NULL) {
        out->server_time_valid = cl_parse_iso8601(server_time->valuestring, &out->server_time_s);
    }

    cJSON_ArrayForEach(item, approvals)
    {
        if (out->approval_count >= CL_MAX_APPROVALS) {
            break;
        }
        if (parse_approval(item, &out->approvals[out->approval_count],
                           &local_stats.truncated_strings)) {
            ++out->approval_count;
        } else {
            ++local_stats.skipped_approvals;
        }
    }
    cJSON_ArrayForEach(item, tasks)
    {
        if (out->task_count >= CL_MAX_TASKS) {
            break;
        }
        if (parse_task(item, &out->tasks[out->task_count], &local_stats.truncated_strings)) {
            ++out->task_count;
        } else {
            ++local_stats.skipped_tasks;
        }
    }
    // 总数不能少于实际展示的条目；服务端数据异常时以列表为准。
    if (out->approvals_total < out->approval_count) {
        out->approvals_total = out->approval_count;
    }
    if (out->tasks_total < out->task_count) {
        out->tasks_total = out->task_count;
    }
    cJSON_Delete(root);
    if (stats != NULL) {
        *stats = local_stats;
    }
    return CL_PARSE_OK;
}

bool cl_device_token_valid(const char *token)
{
    size_t length;

    if (token == NULL || strncmp(token, "awd_", 4) != 0) {
        return false;
    }
    length = strlen(token);
    if (length <= 4 || length >= CL_TOKEN_MAX) {
        return false;
    }
    for (size_t i = 4; i < length; ++i) {
        char c = token[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

cl_parse_result_t cl_parse_pair_response(const char *json, size_t length,
                                         cl_pair_response_t *out)
{
    cJSON *root;
    const cJSON *token;
    bool truncated = false;
    cl_parse_result_t result;

    if (out == NULL) {
        return CL_PARSE_SCHEMA;
    }
    memset(out, 0, sizeof(*out));
    result = parse_root(json, length, &root);
    if (result != CL_PARSE_OK) {
        return result;
    }
    token = get(root, "deviceToken");
    if (!copy_id(root, "deviceId", out->device_id, sizeof(out->device_id)) ||
        strncmp(out->device_id, "dev_", 4) != 0 || !cJSON_IsString(token) ||
        !cl_device_token_valid(token->valuestring) ||
        !copy_optional_string(root, "workspaceName", out->workspace_name,
                              sizeof(out->workspace_name), &truncated)) {
        cJSON_Delete(root);
        memset(out, 0, sizeof(*out));
        return CL_PARSE_SCHEMA;
    }
    strcpy(out->device_token, token->valuestring);
    cJSON_Delete(root);
    return CL_PARSE_OK;
}

bool cl_parse_error_code(const char *json, size_t length, char *code, size_t code_size)
{
    cJSON *root;
    const cJSON *error;
    const cJSON *value;
    bool ok = false;

    if (code == NULL || code_size == 0) {
        return false;
    }
    code[0] = '\0';
    if (parse_root(json, length, &root) != CL_PARSE_OK) {
        return false;
    }
    error = get(root, "error");
    value = cJSON_IsObject(error) ? get(error, "code") : NULL;
    if (cJSON_IsString(value) && value->valuestring != NULL &&
        cl_id_is_safe(value->valuestring) && strlen(value->valuestring) < code_size) {
        strcpy(code, value->valuestring);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

int cl_build_pair_body(char *dst, size_t dst_size, const char *pairing_code, const char *name,
                       const char *firmware_version)
{
    static const char *const keys[] = {"{\"pairingCode\":", ",\"name\":", ",\"firmwareVersion\":"};
    const char *values[] = {pairing_code, name, firmware_version};
    size_t out = 0;

    if (dst == NULL || dst_size == 0) {
        return -1;
    }
    for (size_t i = 0; i < COUNT_OF(keys); ++i) {
        size_t key_len = strlen(keys[i]);
        int quoted;
        if (values[i] == NULL || out + key_len >= dst_size) {
            return -1;
        }
        memcpy(dst + out, keys[i], key_len);
        out += key_len;
        quoted = cl_json_quote(dst + out, dst_size - out, values[i]);
        if (quoted < 0) {
            return -1;
        }
        out += (size_t)quoted;
    }
    if (out + 1 >= dst_size) {
        return -1;
    }
    dst[out++] = '}';
    dst[out] = '\0';
    return (int)out;
}

const char *cl_decision_wire_name(cl_decision_t decision)
{
    switch (decision) {
    case CL_DECISION_ALLOW:
        return "allow";
    case CL_DECISION_ALLOW_ALWAYS:
        return "allow_always";
    case CL_DECISION_DENY:
        return "deny";
    default:
        return NULL;
    }
}

int cl_build_resolve_body(char *dst, size_t dst_size, cl_decision_t decision)
{
    const char *name = cl_decision_wire_name(decision);
    size_t needed;

    if (dst == NULL || name == NULL) {
        return -1;
    }
    needed = strlen("{\"decision\":\"\"}") + strlen(name);
    if (needed + 1 > dst_size) {
        return -1;
    }
    strcpy(dst, "{\"decision\":\"");
    strcat(dst, name);
    strcat(dst, "\"}");
    return (int)needed;
}
