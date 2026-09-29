// main/codeloom_protocol.h — Codeloom 设备协议 v1 的有界 JSON 解析与请求体生成（纯 C + cJSON）。
//
// 解析策略（主机测试覆盖）：
//   - 输入超过 CL_HTTP_RESPONSE_CAP、嵌套深度超过 CL_JSON_MAX_DEPTH、不是 JSON 对象，
//     或顶层缺少/类型错误的 approvals、tasks、approvalsTotal、tasksTotal → 整体拒绝；
//   - 数组只取前 CL_MAX_APPROVALS / CL_MAX_TASKS 项，多余项忽略；
//   - 单个条目类型错误、缺少必需字段或 ID 不安全 → 跳过该条目（计入 skipped）；
//   - 枚举值未知：kind 归为 other，任务 status/runStatus 归为 unknown（仍可显示）；
//   - 字符串按 UTF-8 边界截断到定长缓冲，非法字节替换为 '?'，控制字符折叠为空格。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "codeloom_types.h"

#define CL_JSON_MAX_DEPTH 8

typedef enum {
    CL_PARSE_OK = 0,
    CL_PARSE_TOO_LARGE,
    CL_PARSE_TOO_DEEP,
    CL_PARSE_SYNTAX,
    CL_PARSE_SCHEMA,
} cl_parse_result_t;

typedef struct {
    unsigned skipped_approvals;
    unsigned skipped_tasks;
    bool truncated_strings;
} cl_parse_stats_t;

// 解析 GET /api/v1/device/overview 的响应体。stats 可为 NULL。
cl_parse_result_t cl_parse_overview(const char *json, size_t length, cl_overview_t *out,
                                    cl_parse_stats_t *stats);

typedef struct {
    char device_id[CL_ID_MAX];
    char workspace_name[CL_WORKSPACE_MAX];
    char device_token[CL_TOKEN_MAX];
} cl_pair_response_t;

// 解析 POST /api/v1/devices/pair 的 200 响应。要求 deviceId 以 "dev_" 开头、
// deviceToken 以 "awd_" 开头且只含令牌安全字符；workspaceName 缺失时为空串。
cl_parse_result_t cl_parse_pair_response(const char *json, size_t length,
                                         cl_pair_response_t *out);

// 提取 {"error":{"code":"..."}} 中的 code（仅 ASCII 安全字符）。失败返回 false。
bool cl_parse_error_code(const char *json, size_t length, char *code, size_t code_size);

// 生成配对请求体 {"pairingCode":..,"name":..,"firmwareVersion":..}。
// 返回写入长度，空间不足或参数非法返回 -1。
int cl_build_pair_body(char *dst, size_t dst_size, const char *pairing_code, const char *name,
                       const char *firmware_version);

// 生成审批请求体 {"decision":"allow"|"allow_always"|"deny"}。
int cl_build_resolve_body(char *dst, size_t dst_size, cl_decision_t decision);

// 决策的协议字符串；非法值返回 NULL。
const char *cl_decision_wire_name(cl_decision_t decision);

// 设备令牌是否合法："awd_" 前缀 + [A-Za-z0-9_-]，总长 < CL_TOKEN_MAX。
bool cl_device_token_valid(const char *token);
