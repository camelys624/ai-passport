// main/main.c —— 口袋垂钓 for FoloToy AI Passport：启动与外设初始化。
//
// 启动顺序：I2C → 显示/LVGL → 字体与界面 → NVS → 电量计 → 提示音任务 → 应用任务 → 按键。
// 本固件不包含基线 demo 菜单和测试页面，开机直接进入钓鱼准备页。
// 按键（全局）：
//   准备页  ▲/▼ 换饵，OK 抛竿，长按 OK 打开鱼册
//   等待中  长按 OK 收竿；黑屏时任意键先亮屏
//   咬钩    OK 提竿
//   收线    按屏幕提示按 ▲ / ▼ / OK
//   结果页  OK 继续
//   鱼册    ▲/▼ 翻页，OK 返回
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "fish_app.h"
#include "fish_audio.h"
#include "fish_fonts.h"
#include "fish_store.h"
#include "fish_ui.h"

static const char *TAG = "fishing";

// 按键回调运行在共享 esp_timer 任务中：只投递事件，立即返回。
static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    fish_app_post_key(button, event);
}

void app_main(void)
{
    esp_err_t err;
    bool battery_ok;

    ESP_LOGI(TAG, "口袋垂钓启动");
    bsp_i2c_init();

    // 屏幕是唯一的交互载体，失败就无法继续。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败 (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)", BSP_LCD_MOSI,
                 BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    // 背光由应用任务在第一帧画好后打开。
    bsp_display_backlight(0);
    if (bsp_lvgl_lock(1000)) {
        fish_ui_init();
        if (fish_fonts_self_check() != 0) {
            ESP_LOGE(TAG, "字体覆盖不完整，界面可能出现缺字占位框");
        }
        bsp_lvgl_unlock();
    }

    (void)fish_store_init(); // 失败时照常游戏，只是不保存进度
    battery_ok = bsp_battery_init() == ESP_OK;
    if (!battery_ok) {
        ESP_LOGW(TAG, "电量计不可用，隐藏电量显示");
    }
    err = fish_audio_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提示音任务创建失败: %s，游戏将没有声音", esp_err_to_name(err));
    }

    err = fish_app_start(battery_ok);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "应用任务创建失败: %s", esp_err_to_name(err));
        return;
    }
    err = bsp_button_init(on_key, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
    }
}
