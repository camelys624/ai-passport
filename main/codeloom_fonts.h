// main/codeloom_fonts.h — Codeloom 中文字体（Noto Sans SC 子集）与动态文本字形策略。
//
// 字号与覆盖范围（见 assets/README.md 与 tools/gen_codeloom_fonts.py）：
//   body  16 px：ASCII + 常用标点 + GB2312 一级汉字 + 全部界面文字 —— 用于网络/用户内容
//   small 14 px：ASCII + 常用标点 + 全部界面文字 —— 只用于固定界面文字、数字和 ASCII
//   title 22 px：ASCII + 常用标点 + 全部界面文字 —— 页标题、配网热点名/口令
// 三者都以 Montserrat（14/14/20）为回退，只用于 LV_SYMBOL_* 图标。
//
// 不在字体中的动态字符（emoji、生僻字）由 codeloom_fonts_sanitize() 替换为 “□”
// （U+25A1，已包含在所有字号中），这是有意的可见替换，而不是隐藏缺字。
#pragma once

#include <stddef.h>

#include "lvgl.h"

extern const lv_font_t *cl_font_small;
extern const lv_font_t *cl_font_body;
extern const lv_font_t *cl_font_title;

// 在创建任何标签前调用一次（LVGL 任务内或持有 bsp_lvgl_lock()）。
void codeloom_fonts_init(void);

// 把 src 复制到 dst，font（含回退链）无法显示的码点替换为 “□”，非法 UTF-8 替换为 '?'。
// 保证输出是合法 UTF-8 并以 NUL 结尾；放不下时在字符边界截断。
void codeloom_fonts_sanitize(const lv_font_t *font, const char *src, char *dst, size_t dst_size);

// 启动自检：检查字体能显示 GB2312 一级汉字首尾字、常用标点和图标；缺失项打印 U+XXXX。
// 返回缺失码点个数（0 = 通过）。
int codeloom_fonts_self_check(void);
