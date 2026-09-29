// main/fish_store.h — 进度的 NVS 存取（命名空间 "fishing"，键 "save"）。
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "fish_game.h"

// 初始化 NVS 分区。失败时不自动擦除，调用方以“不保存”模式继续运行。
esp_err_t fish_store_init(void);

// 读取存档；没有存档、格式不符或读取失败时返回 false，out 置为全新进度。
bool fish_store_load(fg_progress_t *out);

esp_err_t fish_store_save(const fg_progress_t *progress);
