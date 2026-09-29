// main/main.c —— Codeloom for FoloToy AI Passport：启动、外设初始化与模式选择。
//
// 启动顺序：I2C → 显示/LVGL → 字体与界面外框 → 事件队列与按键 → 电量计 → NVS。
// 有有效配置时进入正常模式（codeloom_app_task），否则进入配网模式（codeloom_setup_task）。
// 本固件不包含基线 demo 菜单和测试页面；音频在第一次提示音时才初始化。
#include <stdio.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "codeloom_app.h"
#include "codeloom_diag.h"
#include "codeloom_events.h"
#include "codeloom_fonts.h"
#include "codeloom_setup.h"
#include "codeloom_store.h"
#include "codeloom_ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define APP_TASK_STACK 6144
#define SETUP_TASK_STACK 6144
#define APP_TASK_PRIORITY 5

static const char *TAG = "codeloom";

// 正常模式的配置在整个运行期间有效，应用任务按指针持有。
static cl_config_t s_config;

// 按键回调运行在共享 esp_timer 任务中：只投递事件，立即返回。
static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    cl_event_t input = {.type = CL_EVENT_KEY};
    input.key.button = (uint8_t)button;
    input.key.gesture = (uint8_t)event;
    (void)codeloom_events_post(&input, 0);
}

void app_main(void)
{
    char firmware[CL_FIRMWARE_VERSION_MAX];
    esp_err_t err;
    bool configured;

    codeloom_firmware_version(firmware, sizeof(firmware));
    ESP_LOGI(TAG, "Codeloom for AI Passport %s 启动", firmware);
    codeloom_log_heap("boot");

    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败 (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)", BSP_LCD_MOSI,
                 BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    if (bsp_lvgl_lock(1000)) {
        codeloom_fonts_init();
        codeloom_ui_init();
        if (codeloom_fonts_self_check() != 0) {
            ESP_LOGE(TAG, "字体覆盖不完整，界面可能出现 □");
        }
        bsp_lvgl_unlock();
    }
    bsp_display_backlight(80);

    err = codeloom_events_init();
    if (err == ESP_OK) {
        err = bsp_button_init(on_key, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
    }
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用，隐藏电量指示");
    }

    err = codeloom_store_init();
    configured = err == ESP_OK && codeloom_store_load(&s_config) == CL_SETTINGS_OK;
    codeloom_log_heap("peripherals ready");

    if (configured) {
        ESP_LOGI(TAG, "已配对（%s），进入正常模式", s_config.device_id);
        if (xTaskCreate(codeloom_app_task, "cl_app", APP_TASK_STACK, &s_config, APP_TASK_PRIORITY,
                        NULL) != pdPASS) {
            ESP_LOGE(TAG, "应用任务创建失败");
        }
    } else {
        ESP_LOGI(TAG, "没有有效配置，进入配网模式");
        if (xTaskCreate(codeloom_setup_task, "cl_setup", SETUP_TASK_STACK, NULL,
                        APP_TASK_PRIORITY, NULL) != pdPASS) {
            ESP_LOGE(TAG, "配网任务创建失败");
        }
    }
}
