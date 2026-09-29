// main/fish_save.h — 进度存档的二进制编码（纯 C，主机可测）。
//
// 布局（小端）：'F' 'S' 版本(1) 饵料(1) 抛竿次数(u32) 跑鱼次数(u32) 渔获数量(u16 × FISH_ENTRY_MAX)
// 渔获数量按 FISH_ENTRIES 下标存放；预留到 FISH_ENTRY_MAX，追加渔获不需要改格式。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fish_game.h"

#define FISH_SAVE_VERSION 1U
#define FISH_SAVE_SIZE (2U + 1U + 1U + 4U + 4U + 2U * FISH_ENTRY_MAX)

void fish_save_encode(const fg_progress_t *progress, uint8_t out[FISH_SAVE_SIZE]);

// 魔数、版本或长度不符时返回 false 且不修改 out。
bool fish_save_decode(const uint8_t *data, size_t length, fg_progress_t *out);
