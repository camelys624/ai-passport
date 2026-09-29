// main/codeloom_app.c — 见 codeloom_app.h。
//
// 任务划分：
//   按键回调（esp_timer 任务）→ 事件队列 → 本任务（状态机、背光、界面，持锁更新 LVGL）
//   cl_sync 任务：全部 HTTP；结果以事件返回
//   cl_audio 任务：提示音
// 本任务不做任何网络 I/O；只有“重置配置”会在本任务中写 NVS，随后立即重启。
#include "codeloom_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "codeloom_audio.h"
#include "codeloom_diag.h"
#include "codeloom_events.h"
#include "codeloom_state.h"
#include "codeloom_store.h"
#include "codeloom_sync.h"
#include "codeloom_timing.h"
#include "codeloom_ui.h"
#include "codeloom_url.h"
#include "codeloom_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LOOP_WAIT_MS 250
#define RENDER_PERIOD_MS 1000U
#define BATTERY_PERIOD_MS 10000U
#define MEMORY_LOG_PERIOD_MS 60000U

static const char *TAG = "cl_app";

static cl_state_t s_state;
static cl_status_info_t s_info;
static cl_backlight_t s_backlight;
static uint64_t s_last_render_ms;
static uint64_t s_battery_at_ms;
static bool s_logged_wifi_up;
static uint64_t s_memory_log_at_ms;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000U;
}

static void render(uint64_t now)
{
    codeloom_heap_info(&s_info.heap);
    if (bsp_lvgl_lock(200)) {
        codeloom_ui_main_render(&s_state, &s_info, now);
        bsp_lvgl_unlock();
    }
    s_last_render_ms = now;
}

static void apply_backlight(bool woke_from_off)
{
    bsp_display_backlight(cl_backlight_percent(s_backlight.screen));
    codeloom_sync_set_screen_off(s_backlight.screen == CL_SCREEN_OFF);
    if (woke_from_off) {
        codeloom_sync_request_poll(); // 熄屏期间轮询较慢，点亮后立刻刷新
    }
}

static void do_reset(void)
{
    ESP_LOGW(TAG, "用户确认重置配置");
    if (bsp_lvgl_lock(1000)) {
        codeloom_ui_setup_show_progress(CL_SETUP_STAGE_WORKING, "正在重置", "即将重启进入设置模式");
        bsp_lvgl_unlock();
    }
    if (codeloom_store_erase() != CL_SETTINGS_OK) {
        ESP_LOGE(TAG, "清除配置失败");
    }
    codeloom_wifi_forget();
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static void run_actions(const cl_actions_t *actions, uint64_t now)
{
    if (actions->reset) {
        do_reset();
    }
    if (actions->resolve && !codeloom_sync_submit_resolve(actions->resolve_id, actions->decision)) {
        cl_actions_t follow = {0};
        cl_state_resolve_result(&s_state, actions->resolve_id, CL_RESOLVE_FAILED, now, &follow);
    }
    if (actions->alert) {
        bool was_off = s_backlight.screen == CL_SCREEN_OFF;
        if (cl_backlight_wake(&s_backlight, now)) {
            apply_backlight(was_off);
        }
        codeloom_audio_chime();
    }
    if (actions->poll_now) {
        codeloom_sync_request_poll();
    }
    if (actions->render || actions->resolve || actions->alert) {
        render(now);
    }
}

static void feed_key(cl_key_t key, uint64_t now, cl_actions_t *actions)
{
    cl_state_key(&s_state, key, now, actions);
}

static void handle_key(uint8_t button, uint8_t gesture, uint64_t now)
{
    cl_actions_t actions = {0};
    bool changed = false;
    bool was_off = s_backlight.screen == CL_SCREEN_OFF;

    if (gesture == BSP_BTN_PRESS) {
        if (cl_backlight_press(&s_backlight, button, now)) {
            apply_backlight(was_off);
        }
        return;
    }
    if (cl_backlight_gesture_swallowed(&s_backlight, button, now, &changed)) {
        if (changed) {
            apply_backlight(was_off);
        }
        return; // 熄屏/变暗时的第一次按键只用于唤醒
    }
    switch (gesture) {
    case BSP_BTN_CLICK:
    case BSP_BTN_DOUBLE:
        if (button == BSP_BTN_OK) {
            feed_key(CL_KEY_OK, now, &actions);
        } else {
            cl_key_t key = button == BSP_BTN_UP ? CL_KEY_UP : CL_KEY_DOWN;
            feed_key(key, now, &actions);
            if (gesture == BSP_BTN_DOUBLE) {
                feed_key(key, now, &actions); // 双击 = 连续移动两步
            }
        }
        break;
    case BSP_BTN_LONG:
        feed_key(button == BSP_BTN_OK   ? CL_KEY_OK_LONG
                 : button == BSP_BTN_UP ? CL_KEY_UP_LONG
                                        : CL_KEY_DOWN_LONG,
                 now, &actions);
        break;
    default:
        return;
    }
    run_actions(&actions, now);
}

static void handle_event(const cl_event_t *event, uint64_t now)
{
    cl_actions_t actions = {0};

    switch (event->type) {
    case CL_EVENT_KEY:
        handle_key(event->key.button, event->key.gesture, now);
        return;
    case CL_EVENT_OVERVIEW: {
        cl_overview_t *overview = codeloom_sync_take_overview();
        if (overview == NULL) {
            return; // 同一份数据的重复通知
        }
        cl_state_overview(&s_state, overview, now, &actions);
        free(overview);
        break;
    }
    case CL_EVENT_POLL_FAILED:
        cl_state_poll_failed(&s_state, event->poll_error, &actions);
        break;
    case CL_EVENT_RESOLVE_DONE:
        cl_state_resolve_result(&s_state, event->resolve.id, event->resolve.outcome, now,
                                &actions);
        break;
    default:
        return;
    }
    run_actions(&actions, now);
}

static void periodic(uint64_t now)
{
    cl_actions_t actions = {0};
    bool was_on = s_backlight.screen != CL_SCREEN_OFF;
    codeloom_wifi_info_t wifi;

    if (cl_backlight_tick(&s_backlight, now)) {
        apply_backlight(false);
    }
    cl_state_tick(&s_state, now, &actions);

    codeloom_wifi_get_info(&wifi);
    if (wifi.connected != s_state.wifi_connected) {
        cl_state_wifi(&s_state, wifi.connected, &actions);
        if (wifi.connected && !s_logged_wifi_up) {
            s_logged_wifi_up = true;
            codeloom_log_heap("after Wi-Fi up");
        }
    }
    s_info.wifi = wifi;

    if (s_battery_at_ms == 0 || now - s_battery_at_ms >= BATTERY_PERIOD_MS) {
        int soc = bsp_battery_soc();
        s_battery_at_ms = now;
        if (bsp_lvgl_lock(200)) {
            codeloom_ui_set_battery(soc);
            bsp_lvgl_unlock();
        }
    }
    if (now - s_memory_log_at_ms >= MEMORY_LOG_PERIOD_MS) {
        s_memory_log_at_ms = now;
        codeloom_log_heap("periodic");
        if (bsp_lvgl_lock(200)) {
            codeloom_ui_log_memory();
            bsp_lvgl_unlock();
        }
    }
    run_actions(&actions, now);
    // 屏幕亮着时每秒原位刷新时间/内存等动态文本；熄屏后不绘制。
    if (was_on && s_backlight.screen != CL_SCREEN_OFF && now - s_last_render_ms >= RENDER_PERIOD_MS) {
        render(now);
    }
}

static void prepare_status_info(const cl_config_t *config)
{
    cl_url_t url;

    memset(&s_info, 0, sizeof(s_info));
    snprintf(s_info.ssid, sizeof(s_info.ssid), "%s", config->ssid);
    if (cl_url_parse(config->server_url, &url)) {
        char host[CL_HOST_MAX + 8];
        cl_url_display_host(&url, host, sizeof(host));
        // 状态页显示 host[:port]，HTTPS 额外标出协议。
        snprintf(s_info.server_host, sizeof(s_info.server_host), "%s%s",
                 url.https ? "https://" : "", host);
    }
    snprintf(s_info.workspace, sizeof(s_info.workspace), "%s", config->workspace_name);
    snprintf(s_info.device_id, sizeof(s_info.device_id), "%s", config->device_id);
    codeloom_firmware_version(s_info.firmware, sizeof(s_info.firmware));
}

void codeloom_app_task(void *arg)
{
    const cl_config_t *config = arg;
    cl_event_t event;
    esp_err_t err;

    cl_state_init(&s_state);
    prepare_status_info(config);
    cl_backlight_init(&s_backlight, now_ms());
    apply_backlight(false);

    if (bsp_lvgl_lock(1000)) {
        codeloom_ui_main_show();
        codeloom_ui_main_render(&s_state, &s_info, now_ms());
        bsp_lvgl_unlock();
    }
    err = codeloom_audio_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提示音任务启动失败: %s", esp_err_to_name(err));
    }
    err = codeloom_wifi_start_sta(config->ssid, config->password, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 启动失败: %s", esp_err_to_name(err));
    }
    err = codeloom_sync_start(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "同步任务启动失败: %s", esp_err_to_name(err));
    }

    for (;;) {
        if (codeloom_events_receive(&event, LOOP_WAIT_MS)) {
            handle_event(&event, now_ms());
        }
        periodic(now_ms());
    }
}
