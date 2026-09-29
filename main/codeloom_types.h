// main/codeloom_types.h — Codeloom 设备端共享常量与数据结构（纯 C，无 ESP-IDF/LVGL 依赖）。
//
// 字段上限来自 Codeloom ⇄ AI Passport 设备协议 v1：服务端按 Unicode 码点截断字符串，
// 设备端按“码点数 × 3 字节 + NUL”预留缓冲（覆盖全部 BMP 字符，含中文），超出时再按
// UTF-8 边界截断并追加省略号。所有结构体都是定长的，不做运行时分配。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CL_UTF8_CAP(chars) ((chars) * 3 + 1)

// 协议规定 overview 序列化后不超过 8192 字节；设备接收时再留一小段余量，超过即中止。
#define CL_OVERVIEW_BODY_MAX 8192
#define CL_HTTP_RESPONSE_CAP (CL_OVERVIEW_BODY_MAX + 512)

#define CL_MAX_APPROVALS 8
#define CL_MAX_TASKS 12

// 审批/任务/设备 ID：只接受 URL 安全字符，直接拼进 /api/v1/approvals/{id}/resolve。
#define CL_ID_MAX 64
#define CL_WORKSPACE_MAX CL_UTF8_CAP(40)
#define CL_APPROVAL_TITLE_MAX CL_UTF8_CAP(96)
#define CL_APPROVAL_DETAIL_MAX CL_UTF8_CAP(200)
#define CL_TASK_TITLE_MAX CL_UTF8_CAP(60)

// 持久化配置字段上限（字节，含 NUL）。
#define CL_SSID_MAX 33          // 802.11 SSID 最长 32 字节
#define CL_WIFI_PASSWORD_MAX 65 // WPA2 口令 8..63 字符或 64 位十六进制 PSK
#define CL_URL_MAX 128
#define CL_TOKEN_MAX 128
#define CL_PAIRING_CODE_MAX 96
#define CL_FIRMWARE_VERSION_MAX 33 // 协议：firmwareVersion ≤ 32

typedef enum {
    CL_KIND_TOOL = 0,
    CL_KIND_FILE_WRITE,
    CL_KIND_SHELL,
    CL_KIND_NETWORK,
    CL_KIND_OTHER,
} cl_kind_t;

typedef enum {
    CL_TASK_BACKLOG = 0,
    CL_TASK_TODO,
    CL_TASK_IN_PROGRESS,
    CL_TASK_NEEDS_REVIEW,
    CL_TASK_DONE,
    CL_TASK_CANCELED,
    CL_TASK_UNKNOWN,
} cl_task_status_t;

typedef enum {
    CL_RUN_NONE = 0, // JSON null：任务还没有运行记录
    CL_RUN_PENDING,
    CL_RUN_ACTIVE,
    CL_RUN_IDLE,
    CL_RUN_WAITING_APPROVAL,
    CL_RUN_COMPLETED,
    CL_RUN_FAILED,
    CL_RUN_CANCELED,
    CL_RUN_LOST,
    CL_RUN_UNKNOWN,
} cl_run_status_t;

typedef struct {
    char id[CL_ID_MAX];
    cl_kind_t kind;
    char title[CL_APPROVAL_TITLE_MAX];
    char detail[CL_APPROVAL_DETAIL_MAX];
    char task_title[CL_TASK_TITLE_MAX];
    int64_t created_at_s; // Unix 秒；created_valid 为 false 时无意义
    bool created_valid;
} cl_approval_t;

typedef struct {
    char id[CL_ID_MAX];
    char title[CL_TASK_TITLE_MAX];
    cl_task_status_t status;
    cl_run_status_t run_status;
} cl_task_t;

typedef struct {
    int64_t server_time_s;
    bool server_time_valid;
    char workspace_name[CL_WORKSPACE_MAX];
    uint32_t approvals_total;
    uint8_t approval_count;
    cl_approval_t approvals[CL_MAX_APPROVALS];
    uint32_t tasks_total;
    uint8_t task_count;
    cl_task_t tasks[CL_MAX_TASKS];
} cl_overview_t;

typedef enum {
    CL_DECISION_ALLOW = 0,
    CL_DECISION_ALLOW_ALWAYS,
    CL_DECISION_DENY,
    CL_DECISION_COUNT,
} cl_decision_t;

// 已配对设备的持久化配置。Wi-Fi 口令和设备令牌是机密：任何模块都不得打印。
typedef struct {
    char ssid[CL_SSID_MAX];
    char password[CL_WIFI_PASSWORD_MAX];
    char server_url[CL_URL_MAX];
    char device_token[CL_TOKEN_MAX];
    char device_id[CL_ID_MAX];
    char workspace_name[CL_WORKSPACE_MAX];
} cl_config_t;
