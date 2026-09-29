// main/codeloom_ui.h — Codeloom 界面（LVGL）。
//
// 所有函数都必须在 LVGL 任务中调用，或由调用方持有 bsp_lvgl_lock()。
// 正常模式页面：审批 / 审批详情 / 任务 / 状态（含重置确认）；配网模式：热点说明 / 进度。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codeloom_diag.h"
#include "codeloom_state.h"
#include "codeloom_wifi.h"

typedef struct {
    codeloom_wifi_info_t wifi;
    char ssid[CL_SSID_MAX];
    char server_host[96];
    char workspace[CL_WORKSPACE_MAX];
    char device_id[CL_ID_MAX];
    char firmware[CL_FIRMWARE_VERSION_MAX];
    cl_heap_info_t heap;
} cl_status_info_t;

void codeloom_ui_init(void);

// 右上角电量；soc < 0 表示读不到，隐藏指示而不是显示数字。
void codeloom_ui_set_battery(int soc);

// 打印 LVGL 内存池占用（used / max_used / 碎片率），用于真机预算核对。
void codeloom_ui_log_memory(void);

// ---- 正常模式 ----
void codeloom_ui_main_show(void);
void codeloom_ui_main_render(const cl_state_t *state, const cl_status_info_t *info,
                             uint64_t now_ms);

// ---- 配网模式 ----
// status_line 为底部状态；error_line 非空时以红色显示上次失败原因。
void codeloom_ui_setup_show_ap(const char *ap_ssid, const char *ap_password, const char *url,
                               const char *status_line, const char *error_line);

typedef enum {
    CL_SETUP_STAGE_WORKING = 0,
    CL_SETUP_STAGE_FAILED,
    CL_SETUP_STAGE_SUCCESS,
} cl_setup_stage_t;

void codeloom_ui_setup_show_progress(cl_setup_stage_t stage, const char *title,
                                     const char *detail);
