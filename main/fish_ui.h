// main/fish_ui.h — 口袋垂钓的 LVGL 界面（240 × 320 竖屏）。
//
// 页面：准备（选饵）/ 等待 / 咬钩 / 收线 / 结果 / 鱼册，全部由 fish_game_t 的状态渲染，
// 界面本身不保存玩法状态。所有函数必须在 LVGL 任务内或持有 bsp_lvgl_lock() 时调用。
// battery 为电量百分比，-1 表示不可用（不显示电量）。
#pragma once

#include <stdint.h>

#include "fish_game.h"

void fish_ui_init(void);

// 重建当前页面（页面切换、换饵、鱼册翻页、亮屏后调用）。
void fish_ui_render(const fish_game_t *game, uint32_t now_ms, int battery);

// 刷新当前页面的动态部分：电量、已等待时间、倒计时条、收线提示与进度。
void fish_ui_refresh(const fish_game_t *game, uint32_t now_ms, int battery);

// 黑屏前停止装饰动画，避免背光关闭时继续刷屏。亮屏后用 fish_ui_render() 恢复。
void fish_ui_pause(void);
