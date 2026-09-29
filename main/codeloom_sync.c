// main/codeloom_sync.c — 见 codeloom_sync.h。
#include "codeloom_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codeloom_diag.h"
#include "codeloom_events.h"
#include "codeloom_http.h"
#include "codeloom_protocol.h"
#include "codeloom_timing.h"
#include "codeloom_url.h"
#include "codeloom_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SYNC_TASK_STACK 8192
#define SYNC_TASK_PRIORITY 4
#define HTTP_TIMEOUT_MS 8000
#define URL_BUFFER (CL_URL_MAX + 48 + CL_ID_MAX)

typedef enum {
    CMD_RESOLVE = 0,
    CMD_POLL,
} sync_cmd_type_t;

typedef struct {
    sync_cmd_type_t type;
    cl_decision_t decision;
    char id[CL_ID_MAX];
} sync_cmd_t;

static const char *TAG = "cl_sync";

static cl_config_t s_config;
static QueueHandle_t s_commands;
static SemaphoreHandle_t s_handoff_lock;
// 已解析、尚未被应用任务取走的 overview（堆上约 12 KB）。只在解析时短暂分配：
// 解析发生在 HTTP/TLS 资源释放之后，稳态不占用第二份静态副本。
static cl_overview_t *s_pending;
static volatile bool s_screen_off;
static char s_body[CL_HTTP_RESPONSE_CAP + 1];
static char s_url[URL_BUFFER];
static unsigned s_failures;
static bool s_revoked;
static bool s_logged_first_request;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000U;
}

static void post_poll_failed(cl_poll_error_t error)
{
    cl_event_t event = {.type = CL_EVENT_POLL_FAILED, .poll_error = error};
    codeloom_events_post(&event, 100);
}

static void post_resolve(const char *id, cl_resolve_outcome_t outcome)
{
    cl_event_t event = {.type = CL_EVENT_RESOLVE_DONE};
    snprintf(event.resolve.id, sizeof(event.resolve.id), "%s", id);
    event.resolve.outcome = outcome;
    // 结果必须送达，否则审批锁无法释放：必要时等待应用任务腾出队列。
    while (!codeloom_events_post(&event, 1000)) {
        ESP_LOGW(TAG, "事件队列已满，重试投递审批结果");
    }
}

static void log_first_request(void)
{
    if (!s_logged_first_request) {
        s_logged_first_request = true;
        codeloom_log_heap("after first request");
    }
}

static void do_poll(void)
{
    cl_http_response_t response;
    cl_parse_result_t parsed;
    cl_parse_stats_t stats;
    cl_poll_error_t error;

    if (!cl_url_join(s_url, sizeof(s_url), s_config.server_url, "/api/v1/device/overview")) {
        post_poll_failed(CL_POLL_BAD_RESPONSE);
        return;
    }
    codeloom_http_request(CL_HTTP_GET, s_url, s_config.device_token, NULL, s_body, sizeof(s_body),
                          HTTP_TIMEOUT_MS, &response);
    log_first_request();
    if (response.error != ESP_OK) {
        error = CL_POLL_UNREACHABLE;
    } else if (response.status == 401) {
        error = CL_POLL_UNAUTHORIZED;
    } else if (response.status != 200 || response.too_large) {
        ESP_LOGW(TAG, "overview HTTP %d%s", response.status, response.too_large ? " (过大)" : "");
        error = CL_POLL_BAD_RESPONSE;
    } else {
        cl_overview_t *overview = malloc(sizeof(*overview));
        if (overview == NULL) {
            ESP_LOGE(TAG, "overview 缓冲分配失败（%u 字节）", (unsigned)sizeof(*overview));
            parsed = CL_PARSE_TOO_LARGE;
        } else {
            parsed = cl_parse_overview(s_body, response.length, overview, &stats);
        }
        if (parsed == CL_PARSE_OK) {
            cl_overview_t *stale;
            if (stats.skipped_approvals || stats.skipped_tasks) {
                ESP_LOGW(TAG, "忽略了 %u 条审批、%u 条任务（字段无效）", stats.skipped_approvals,
                         stats.skipped_tasks);
            }
            xSemaphoreTake(s_handoff_lock, portMAX_DELAY);
            stale = s_pending; // 应用任务还没取走上一份：以最新的为准
            s_pending = overview;
            xSemaphoreGive(s_handoff_lock);
            free(stale);
            s_failures = 0;
            s_revoked = false;
            cl_event_t event = {.type = CL_EVENT_OVERVIEW};
            codeloom_events_post(&event, 100);
            return;
        }
        free(overview);
        ESP_LOGW(TAG, "overview 解析失败: %d", (int)parsed);
        error = CL_POLL_BAD_RESPONSE;
    }
    if (error == CL_POLL_UNAUTHORIZED) {
        s_revoked = true;
    } else if (s_failures < 16U) {
        s_failures++;
    }
    post_poll_failed(error);
}

static void do_resolve(const sync_cmd_t *cmd)
{
    cl_http_response_t response;
    char body[48];
    cl_resolve_outcome_t outcome;
    int written = snprintf(s_url, sizeof(s_url), "%s/api/v1/approvals/%s/resolve",
                           s_config.server_url, cmd->id);

    if (!cl_id_is_safe(cmd->id) || written <= 0 || (size_t)written >= sizeof(s_url) ||
        cl_build_resolve_body(body, sizeof(body), cmd->decision) < 0) {
        post_resolve(cmd->id, CL_RESOLVE_FAILED);
        return;
    }
    codeloom_http_request(CL_HTTP_POST, s_url, s_config.device_token, body, NULL, 0,
                          HTTP_TIMEOUT_MS, &response);
    log_first_request();
    if (response.error != ESP_OK) {
        outcome = CL_RESOLVE_FAILED;
    } else if (response.status >= 200 && response.status < 300) {
        outcome = CL_RESOLVE_OK;
    } else if (response.status == 409) {
        outcome = CL_RESOLVE_CONFLICT;
    } else if (response.status == 404) {
        outcome = CL_RESOLVE_NOT_FOUND;
    } else if (response.status == 401) {
        outcome = CL_RESOLVE_UNAUTHORIZED;
        s_revoked = true;
    } else {
        outcome = CL_RESOLVE_FAILED;
    }
    ESP_LOGI(TAG, "审批 %s → %s: HTTP %d", cmd->id, cl_decision_wire_name(cmd->decision),
             response.status);
    post_resolve(cmd->id, outcome);
}

static void sync_task(void *arg)
{
    uint64_t next_poll_ms = 0;
    sync_cmd_t cmd;

    (void)arg;
    for (;;) {
        uint64_t now;
        uint32_t wait_ms;

        if (!codeloom_wifi_wait_connected(0)) {
            // 离线时审批提交立即失败，避免锁一直挂着；恢复连接后立刻轮询。
            if (xQueueReceive(s_commands, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE &&
                cmd.type == CMD_RESOLVE) {
                post_resolve(cmd.id, CL_RESOLVE_FAILED);
            }
            next_poll_ms = 0;
            continue;
        }
        now = now_ms();
        wait_ms = next_poll_ms > now ? (uint32_t)(next_poll_ms - now) : 0;
        if (xQueueReceive(s_commands, &cmd, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
            if (cmd.type == CMD_RESOLVE) {
                do_resolve(&cmd);
            }
            next_poll_ms = 0; // 提交后或收到请求时立即刷新
            continue;
        }
        do_poll();
        next_poll_ms = now_ms() + cl_poll_delay_ms(s_screen_off, s_failures, s_revoked);
    }
}

esp_err_t codeloom_sync_start(const cl_config_t *config)
{
    if (s_commands != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_config = *config;
    s_handoff_lock = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(6, sizeof(sync_cmd_t));
    if (s_handoff_lock == NULL || s_commands == NULL ||
        xTaskCreate(sync_task, "cl_sync", SYNC_TASK_STACK, NULL, SYNC_TASK_PRIORITY, NULL) !=
            pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool codeloom_sync_submit_resolve(const char *id, cl_decision_t decision)
{
    sync_cmd_t cmd = {.type = CMD_RESOLVE, .decision = decision};

    if (s_commands == NULL || !cl_id_is_safe(id)) {
        return false;
    }
    snprintf(cmd.id, sizeof(cmd.id), "%s", id);
    return xQueueSend(s_commands, &cmd, 0) == pdTRUE;
}

void codeloom_sync_request_poll(void)
{
    sync_cmd_t cmd = {.type = CMD_POLL};

    if (s_commands != NULL) {
        xQueueSend(s_commands, &cmd, 0);
    }
}

void codeloom_sync_set_screen_off(bool off)
{
    s_screen_off = off;
}

cl_overview_t *codeloom_sync_take_overview(void)
{
    cl_overview_t *overview;

    xSemaphoreTake(s_handoff_lock, portMAX_DELAY);
    overview = s_pending;
    s_pending = NULL;
    xSemaphoreGive(s_handoff_lock);
    return overview;
}
