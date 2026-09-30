// main/fish_sprites.h — 8-bit 像素风素材（构建时由 tools/gen_fish_sprites.py 生成定义）。
//
// 全部是未压缩 RGB565 / RGB565A8，LVGL 直接从 Flash 绘制，不解码、不占额外 RAM。
// 尺寸已按整数倍（×3）放大好，界面上不要再缩放（运行时缩放吃 CPU，也会让像素变糊）。
// 数组下标与 FISH_ENTRIES / FISH_BAITS 一一对应；数量不符时编译失败。
#pragma once

#include "fish_catalog.h"
#include "lvgl.h"

#define FISH_SPRITE_W 96  // 渔获图：32 × 20 像素 × 3
#define FISH_SPRITE_H 60
#define FISH_BAIT_ICON 48 // 饵料图标：16 × 16 像素 × 3
#define FISH_ART_PX 3     // 一个像素点 = 3 屏幕像素；动效按整格移动才有红白机的颗粒感
#define FISH_CLOUD_COUNT 3
#define FISH_WAVE_COUNT 6 // 向左滚动的水波纹，顺序与 fish_ui.c 的 WAVES[] 一致

extern const lv_image_dsc_t fish_img_background;                // 240 × 320 全屏背景
extern const lv_image_dsc_t fish_img_entries[FISH_ENTRY_COUNT];  // 渔获
extern const lv_image_dsc_t fish_img_shadows[FISH_ENTRY_COUNT];  // 未发现的剪影
extern const lv_image_dsc_t fish_img_baits[FISH_BAIT_COUNT];     // 饵料图标
extern const lv_image_dsc_t fish_img_clouds[FISH_CLOUD_COUNT];   // 飘动的云（背景里不含云）
extern const lv_image_dsc_t fish_img_waves[FISH_WAVE_COUNT];     // 水波纹
