// main/fish_app.h — 口袋垂钓应用任务：按键事件、计时、界面、声音、存档的调度中心。
//
// 唯一的玩法状态 fish_game_t 只在应用任务内访问；按键回调只把事件入队。
// 应用任务按 fish_game_ms_until_next() 的最近截止时间阻塞等待按键，黑屏等待咬钩时
// 不做周期性刷新；亮屏时另外按需刷新倒计时条（50 ms）、已等待时间（1 s）和电量（30 s）。
#pragma once

#include <stdbool.h>

#include "bsp_button.h"
#include "esp_err.h"

// 创建事件队列与应用任务。调用前显示/LVGL、字体、NVS 必须已初始化。
// battery_ok 为 false 时不读取电量计、不显示电量。
esp_err_t fish_app_start(bool battery_ok);

// 按键回调入口：只入队、立即返回，可在 esp_timer 任务中调用。
void fish_app_post_key(bsp_btn_t button, bsp_btn_ev_t event);
