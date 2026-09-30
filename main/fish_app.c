// main/fish_app.c — 见 fish_app.h。
#include "fish_app.h"

#include <stdint.h>

#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "fish_audio.h"
#include "fish_game.h"
#include "fish_store.h"
#include "fish_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define APP_TASK_STACK 6144
#define APP_TASK_PRIORITY 5
#define KEY_QUEUE_DEPTH 16
#define FRAME_MS 50U               // 咬钩/收线倒计时条刷新间隔
#define CLOCK_MS 1000U             // 等待页“已等”时间刷新间隔
#define BATTERY_MS 30000U          // 电量读取间隔（仅亮屏时）
#define BACKLIGHT_PERCENT 80
#define BACKLIGHT_SETTLE_MS 40     // 亮屏前等 LVGL 刷完新画面，避免闪出旧帧
#define LVGL_LOCK_MS 500

typedef struct {
    uint8_t button;
    uint8_t event;
} key_event_t;

static const char *TAG = "fish_app";
static QueueHandle_t s_keys;
static fish_game_t s_game;
static bool s_battery_ok;
static int s_battery = -1;
static uint32_t s_battery_ms;
// 页面切换时拿不到 LVGL 锁就记下来，下一轮重试；否则画面会停在旧页面，
// 而玩法已经进入新页面（看起来像卡住）。
static bool s_render_pending;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void read_battery(uint32_t now)
{
    s_battery = s_battery_ok ? bsp_battery_soc() : -1;
    s_battery_ms = now;
}

static bool map_input(uint8_t event, fg_input_t *out)
{
    switch ((bsp_btn_ev_t)event) {
    case BSP_BTN_PRESS:
        *out = FG_INPUT_PRESS;
        return true;
    case BSP_BTN_CLICK:
    case BSP_BTN_DOUBLE: // 快速连按两下时组件只报双击，按一次单击处理
        *out = FG_INPUT_CLICK;
        return true;
    case BSP_BTN_LONG:
        *out = FG_INPUT_LONG;
        return true;
    default:
        return false;
    }
}

// 亮屏期间的周期刷新：返回下一次需要醒来的毫秒数。
static uint32_t periodic_wait(uint32_t now)
{
    uint32_t wait = FG_NO_DEADLINE;

    if (!s_game.screen_on) {
        return wait;
    }
    if (s_render_pending || s_game.view == FG_VIEW_BITE || s_game.view == FG_VIEW_REEL) {
        wait = FRAME_MS;
    } else if (s_game.view == FG_VIEW_WAITING) {
        wait = CLOCK_MS - (now - s_game.cast_ms) % CLOCK_MS;
    }
    if (s_battery_ok) {
        uint32_t since = now - s_battery_ms;
        wait = min_u32(wait, since >= BATTERY_MS ? 0U : BATTERY_MS - since);
    }
    return wait;
}

static void play_sounds(uint32_t fx)
{
    if (fx & FG_FX_SOUND_BITE) {
        fish_audio_play(FISH_SOUND_BITE);
    } else if (fx & FG_FX_SOUND_CATCH) {
        fish_audio_play(FISH_SOUND_CATCH);
    } else if (fx & FG_FX_SOUND_ESCAPE) {
        fish_audio_play(FISH_SOUND_ESCAPE);
    }
}

static void apply(uint32_t fx, uint32_t now, bool periodic)
{
    bool render;
    bool refresh;

    play_sounds(fx);
    // 鱼册卖鱼也会播放收获音，只在结果页记录本竿结果。
    if ((fx & (FG_FX_SOUND_CATCH | FG_FX_SOUND_ESCAPE)) && s_game.view == FG_VIEW_RESULT) {
        static const char *const REASONS[] = {"钓到", "没提竿", "按错键", "收线太慢"};
        if (s_game.outcome == FG_OUTCOME_QUIT) {
            ESP_LOGI(TAG, "放弃专注：已专注 %lu 秒（累计放弃 %lu 次）",
                     (unsigned long)(s_game.focused_ms / 1000U), (unsigned long)s_game.progress.focus_quits);
        } else {
            ESP_LOGI(TAG, "结果：%s %s（累计 %u）", REASONS[s_game.outcome],
                     FISH_ENTRIES[s_game.entry].name, (unsigned)s_game.progress.counts[s_game.entry]);
        }
    }
    if ((fx & FG_FX_SAVE) && s_game.view == FG_VIEW_BITE && FISH_BAITS[s_game.progress.bait].tomatoes > 0) {
        ESP_LOGI(TAG, "专注完成：累计 %lu 个番茄，连续 %lu", (unsigned long)s_game.progress.tomatoes,
                 (unsigned long)s_game.progress.focus_streak);
    }
    if (fx & FG_FX_SAVE) {
        (void)fish_store_save(&s_game.progress);
    }

    if (fx & FG_FX_SCREEN_OFF) {
        if (bsp_lvgl_lock(LVGL_LOCK_MS)) {
            fish_ui_pause();
            bsp_lvgl_unlock();
        }
        bsp_display_backlight(0);
        return;
    }
    if (!s_game.screen_on) {
        return;
    }

    if (fx & FG_FX_SCREEN_ON) {
        read_battery(now);
    } else if (s_battery_ok && now - s_battery_ms >= BATTERY_MS) {
        read_battery(now);
        periodic = true;
    }

    // 鱼册翻页会换掉造型和整张卡片，直接重建；换饵、收线进度等只改动态部分。
    render = s_render_pending || (fx & (FG_FX_REDRAW | FG_FX_SCREEN_ON)) != 0 ||
             ((fx & FG_FX_UPDATE) && s_game.view == FG_VIEW_ALBUM);
    refresh = !render && ((fx & FG_FX_UPDATE) || periodic);
    if (render || refresh) {
        if (!bsp_lvgl_lock(LVGL_LOCK_MS)) {
            ESP_LOGW(TAG, "LVGL 锁超时，%s", render ? "稍后重建页面" : "跳过本次刷新");
            s_render_pending = s_render_pending || render;
        } else {
            if (render) {
                fish_ui_render(&s_game, now, s_battery);
                s_render_pending = false;
            } else {
                fish_ui_refresh(&s_game, now, s_battery);
            }
            bsp_lvgl_unlock();
        }
    }
    if (fx & FG_FX_SCREEN_ON) {
        vTaskDelay(pdMS_TO_TICKS(BACKLIGHT_SETTLE_MS));
        bsp_display_backlight(BACKLIGHT_PERCENT);
    }
}

static void app_task(void *arg)
{
    fg_progress_t saved;
    uint32_t now = now_ms();
    bool loaded = fish_store_load(&saved);

    (void)arg;
    if (loaded) {
        ESP_LOGI(TAG, "读取存档：抛竿 %lu 次，已收集 %u/%u，积分 %lu", (unsigned long)saved.casts,
                 (unsigned)fish_game_collected(&saved), (unsigned)FISH_ENTRY_COUNT,
                 (unsigned long)saved.points);
    }
    fish_game_init(&s_game, loaded ? &saved : NULL, esp_random(), now);
    read_battery(now);
    if (bsp_lvgl_lock(LVGL_LOCK_MS * 2)) {
        fish_ui_render(&s_game, now, s_battery);
        bsp_lvgl_unlock();
    } else {
        s_render_pending = true;
    }
    vTaskDelay(pdMS_TO_TICKS(BACKLIGHT_SETTLE_MS));
    bsp_display_backlight(BACKLIGHT_PERCENT);

    for (;;) {
        key_event_t key;
        fg_input_t input;
        uint32_t fx = 0;
        uint32_t wait;
        TickType_t ticks;
        bool periodic;

        now = now_ms();
        wait = min_u32(fish_game_ms_until_next(&s_game, now), periodic_wait(now));
        ticks = wait == FG_NO_DEADLINE ? portMAX_DELAY : pdMS_TO_TICKS(wait);
        if (xQueueReceive(s_keys, &key, ticks) == pdTRUE) {
            // 连按时一次取完所有已排队的按键，逐个交给玩法，最后只刷新一次界面：
            // 界面更新比按键慢时不会越积越多，画面总是反映最新状态。
            do {
                now = now_ms();
                if (map_input(key.event, &input)) {
                    fx |= fish_game_input(&s_game, (fg_key_t)key.button, input, now);
                }
            } while (xQueueReceive(s_keys, &key, 0) == pdTRUE);
            periodic = false;
        } else {
            now = now_ms();
            periodic = true; // 超时醒来：刷新倒计时条/已等时间
        }
        fx |= fish_game_tick(&s_game, now);
        apply(fx, now, periodic);
    }
}

esp_err_t fish_app_start(bool battery_ok)
{
    s_battery_ok = battery_ok;
    s_keys = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(key_event_t));
    if (s_keys == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(app_task, "fish_app", APP_TASK_STACK, NULL, APP_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_keys);
        s_keys = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void fish_app_post_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    key_event_t input = {.button = (uint8_t)button, .event = (uint8_t)event};

    if (s_keys != NULL && button <= BSP_BTN_OK) {
        (void)xQueueSend(s_keys, &input, 0);
    }
}
