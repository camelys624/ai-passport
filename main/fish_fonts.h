// main/fish_fonts.h — 口袋垂钓的中文字体（Noto Sans SC 子集，tools/gen_fish_fonts.py 生成）。
//
//   body  16 px Regular：ASCII、常用标点、全部界面文字
//   title 24 px Medium ：同上
//   large 40 px Medium ：只含 FISH_LARGE_TEXT（收线提示 ▲ ▼ OK 与“咬钩了！”）
// body/title 以 Montserrat 14/20 为回退，只用于 LV_SYMBOL_* 电池图标。
// 界面只显示固件内的固定文字，没有用户或网络输入的动态文本。
#pragma once

#include "lvgl.h"

// 40 px 字体收录的全部字符；只允许把这些字符交给 fish_font_large。
#define FISH_LARGE_TEXT "▲▼OK咬钩了！"

extern const lv_font_t *fish_font_body;
extern const lv_font_t *fish_font_title;
extern const lv_font_t *fish_font_large;

// 在创建任何标签前调用一次（LVGL 任务内或持有 bsp_lvgl_lock()）。
void fish_fonts_init(void);

// 开机字形自检：逐个码点检查界面字符表；缺失项打印 U+XXXX。返回缺失个数（0 = 通过）。
int fish_fonts_self_check(void);
