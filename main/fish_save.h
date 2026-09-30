// main/fish_save.h — 进度存档的二进制编码（纯 C，主机可测）。
//
// 布局（小端）：'F' 'S' 版本(1) 饵料(1) 抛竿次数(u32) 跑鱼次数(u32) 渔获数量(u16 × FISH_ENTRY_MAX)
//              积分(u32) 鱼篓数量(u16 × FISH_ENTRY_MAX) 完成番茄(u32) 连续番茄(u32) 放弃专注(u32)
// 渔获数量按 FISH_ENTRIES 下标存放；预留到 FISH_ENTRY_MAX，追加渔获不需要改格式。
// 版本 1 没有积分及之后各项：读入时积分记为 FG_DAILY_POINTS、鱼篓为空、专注记录为 0。
// 版本 2 没有末尾三项专注记录：读入时记为 0。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fish_game.h"

#define FISH_SAVE_VERSION 3U
#define FISH_SAVE_V1_SIZE (2U + 1U + 1U + 4U + 4U + 2U * FISH_ENTRY_MAX)
#define FISH_SAVE_V2_SIZE (FISH_SAVE_V1_SIZE + 4U + 2U * FISH_ENTRY_MAX)
#define FISH_SAVE_SIZE (FISH_SAVE_V2_SIZE + 3U * 4U)

void fish_save_encode(const fg_progress_t *progress, uint8_t out[FISH_SAVE_SIZE]);

// 魔数、版本或长度不符时返回 false 且不修改 out。接受版本 1、2 与当前版本。
bool fish_save_decode(const uint8_t *data, size_t length, fg_progress_t *out);
