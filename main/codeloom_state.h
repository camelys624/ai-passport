// main/codeloom_state.h — Codeloom 应用状态机（纯 C reducer，无 ESP-IDF/LVGL 依赖）。
//
// 只在应用任务中调用。输入是按键、同步结果、Wi-Fi 状态、审批提交结果和时钟；输出是
// cl_actions_t（重绘、提醒、提交审批、重置、立即轮询），由应用任务执行副作用。
//
// 审批 ID 锁语义（参考 claude-buddy 的 buddy_state）：
//   - 提交中的 ID 进入 inflight 集合，结果返回前不会再次提交；
//   - 提交成功（含 409/404“已在别处处理”）的 ID 进入 resolved 集合，即使服务端下一次
//     overview 仍短暂返回它，也不再在列表中提供；
//   - 选中项或详情页对应的 ID 从 overview 消失时，选择落到相邻项、详情页关闭并提示。
// 新审批提醒：本次开机后第一次见到的审批 ID（包括开机后第一次同步已存在的待审批）
// 触发一次 alert；不在详情/确认对话框中时跳到审批页并选中该项。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codeloom_types.h"

#define CL_SEEN_CAP 32
#define CL_RESOLVED_CAP 16
#define CL_INFLIGHT_CAP 4
#define CL_STATUS_ROWS 10
#define CL_DETAIL_RESULT_MS 1500U
#define CL_TOAST_MS 2500U
// 连续失败达到该次数才显示“服务器不可达”，避免单次超时造成横幅闪烁。
#define CL_UNREACHABLE_AFTER_FAILURES 2U

typedef enum {
    CL_PAGE_APPROVALS = 0,
    CL_PAGE_TASKS,
    CL_PAGE_STATUS,
    CL_PAGE_COUNT,
} cl_page_t;

typedef enum {
    CL_VIEW_LIST = 0,
    CL_VIEW_DETAIL,
    CL_VIEW_RESET_CONFIRM,
} cl_view_t;

typedef enum {
    CL_LINK_WIFI_CONNECTING = 0,
    CL_LINK_OK,
    CL_LINK_UNREACHABLE,
    CL_LINK_REVOKED,
} cl_link_t;

typedef enum {
    CL_KEY_UP = 0,
    CL_KEY_DOWN,
    CL_KEY_OK,
    CL_KEY_OK_LONG,
    CL_KEY_UP_LONG,
    CL_KEY_DOWN_LONG,
} cl_key_t;

typedef enum {
    CL_DETAIL_IDLE = 0,
    CL_DETAIL_SENDING,
    CL_DETAIL_DONE,      // 本机提交成功
    CL_DETAIL_ELSEWHERE, // 409/404：已在别处处理
    CL_DETAIL_FAILED,    // 网络或服务器错误，可重试
    CL_DETAIL_REVOKED,   // 401：设备已吊销
} cl_detail_status_t;

typedef enum {
    CL_RESOLVE_OK = 0,
    CL_RESOLVE_CONFLICT,  // 409
    CL_RESOLVE_NOT_FOUND, // 404
    CL_RESOLVE_UNAUTHORIZED,
    CL_RESOLVE_FAILED,
} cl_resolve_outcome_t;

typedef enum {
    CL_POLL_UNREACHABLE = 0,
    CL_POLL_UNAUTHORIZED,
    CL_POLL_BAD_RESPONSE,
} cl_poll_error_t;

typedef enum {
    CL_TOAST_NONE = 0,
    CL_TOAST_HANDLED_ELSEWHERE,
    CL_TOAST_QUEUE_FULL,
} cl_toast_t;

// ID 集合：存储数组放在 cl_state_t 内，按各自容量定长，集合只保存指针和计数。
typedef struct {
    char (*ids)[CL_ID_MAX];
    uint8_t capacity;
    uint8_t count;
    uint8_t next; // 满后按 FIFO 覆盖最早的项
} cl_id_set_t;

typedef struct {
    cl_overview_t overview;
    bool has_overview;
    uint64_t synced_at_ms;
    uint32_t overview_gen; // 列表内容变化时递增，UI 据此重建行

    cl_page_t page;
    cl_view_t view;
    bool user_navigated;

    char selected_approval_id[CL_ID_MAX];
    uint8_t approval_sel; // 可见审批列表中的下标
    uint8_t task_sel;
    uint8_t status_row;

    cl_approval_t detail; // 详情页数据快照；审批从 overview 消失后仍可显示提交结果
    uint8_t action_focus; // cl_decision_t 顺序：批准、本次总是允许、拒绝
    uint8_t detail_scroll;
    cl_detail_status_t detail_status;
    cl_decision_t detail_decision;
    uint64_t detail_close_at_ms; // 0 = 不自动关闭

    bool reset_confirm_focus; // true = 焦点在“重置”

    cl_id_set_t seen;
    cl_id_set_t resolved;
    cl_id_set_t inflight;
    char seen_ids[CL_SEEN_CAP][CL_ID_MAX];
    char resolved_ids[CL_RESOLVED_CAP][CL_ID_MAX];
    char inflight_ids[CL_INFLIGHT_CAP][CL_ID_MAX];

    cl_link_t link;
    bool wifi_connected;
    bool revoked;
    unsigned poll_failures;

    cl_toast_t toast;
    uint64_t toast_until_ms;
} cl_state_t;

typedef struct {
    bool render;
    bool alert;    // 响铃并点亮屏幕
    bool resolve;  // 提交 resolve_id / decision
    bool reset;    // 清除配置并重启
    bool poll_now; // 尽快刷新 overview
    char resolve_id[CL_ID_MAX];
    cl_decision_t decision;
} cl_actions_t;

void cl_state_init(cl_state_t *state);

void cl_state_key(cl_state_t *state, cl_key_t key, uint64_t now_ms, cl_actions_t *actions);
void cl_state_overview(cl_state_t *state, const cl_overview_t *overview, uint64_t now_ms,
                       cl_actions_t *actions);
void cl_state_poll_failed(cl_state_t *state, cl_poll_error_t error, cl_actions_t *actions);
void cl_state_wifi(cl_state_t *state, bool connected, cl_actions_t *actions);
void cl_state_resolve_result(cl_state_t *state, const char *id, cl_resolve_outcome_t outcome,
                             uint64_t now_ms, cl_actions_t *actions);
void cl_state_tick(cl_state_t *state, uint64_t now_ms, cl_actions_t *actions);

// 列出可见审批（排除本机已处理的 ID）在 overview.approvals 中的下标，返回数量。
uint8_t cl_state_visible_approvals(const cl_state_t *state, uint8_t indices[CL_MAX_APPROVALS]);
bool cl_state_is_inflight(const cl_state_t *state, const char *id);

// 审批创建距今秒数（基于 serverTime 与本机同步时刻推算）。无法计算时返回 -1。
int64_t cl_state_approval_age_s(const cl_state_t *state, const cl_approval_t *approval,
                                uint64_t now_ms);
