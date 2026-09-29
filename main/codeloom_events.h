// main/codeloom_events.h — 应用事件队列：按键回调、同步任务 → 应用任务。
//
// 按键回调运行在 esp_timer 任务中，只能用 wait_ms = 0 投递。队列满时丢弃事件并返回 false。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codeloom_state.h"
#include "codeloom_types.h"
#include "esp_err.h"

typedef enum {
    CL_EVENT_KEY = 0,      // key.button = bsp_btn_t, key.gesture = bsp_btn_ev_t
    CL_EVENT_OVERVIEW,     // 新 overview 已写入同步邮箱
    CL_EVENT_POLL_FAILED,  // poll_error
    CL_EVENT_RESOLVE_DONE, // resolve.id / resolve.outcome
} cl_event_type_t;

typedef struct {
    cl_event_type_t type;
    union {
        struct {
            uint8_t button;
            uint8_t gesture;
        } key;
        cl_poll_error_t poll_error;
        struct {
            char id[CL_ID_MAX];
            cl_resolve_outcome_t outcome;
        } resolve;
    };
} cl_event_t;

esp_err_t codeloom_events_init(void);
bool codeloom_events_post(const cl_event_t *event, uint32_t wait_ms);
bool codeloom_events_receive(cl_event_t *event, uint32_t wait_ms);
