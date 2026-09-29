// main/codeloom_sync.h — 正常模式的同步工作任务：轮询 overview、提交审批。
//
// 任务独占全部 HTTP 调用；应用任务只通过本接口投递命令，结果以 cl_event_t 返回。
// 解析好的 overview 放在短暂分配的堆缓冲中，应用任务收到 CL_EVENT_OVERVIEW 后
// 用 codeloom_sync_take_overview() 取走并负责 free()。
#pragma once

#include <stdbool.h>

#include "codeloom_types.h"
#include "esp_err.h"

// 启动任务。config 在内部复制；返回后调用方可释放。
esp_err_t codeloom_sync_start(const cl_config_t *config);

// 投递审批提交。队列满时返回 false（调用方应按失败处理，释放锁）。
bool codeloom_sync_submit_resolve(const char *id, cl_decision_t decision);

// 请求尽快轮询一次（例如屏幕从熄灭唤醒）。
void codeloom_sync_request_poll(void);

// 屏幕熄灭时轮询间隔从 5 s 放宽到 20 s。
void codeloom_sync_set_screen_off(bool off);

// 取走最新一份 overview 的所有权（调用方用 free() 释放）；没有新数据时返回 NULL。
cl_overview_t *codeloom_sync_take_overview(void);
