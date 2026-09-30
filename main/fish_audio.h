// main/fish_audio.h — 提示音工作任务。
//
// fish_audio_play() 不阻塞，可在应用任务中调用：只覆盖写入长度为 1 的请求队列。
// 工作任务启动时初始化 ES8311/I2S 后立即让 codec 休眠；每次播放前唤醒、播放后休眠，
// 空闲时 codec 处于低功耗状态，咬钩提示也不必等待冷启动初始化。
#pragma once

#include "esp_err.h"
#include "fish_sound.h"

esp_err_t fish_audio_start(void);
void fish_audio_play(fish_sound_t sound);
