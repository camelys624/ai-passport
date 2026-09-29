// main/codeloom_audio.h — 提示音工作任务。
//
// codeloom_audio_chime() 可在应用任务中调用，不阻塞：只覆盖写入长度为 1 的请求队列。
// 工作任务按需初始化 ES8311/I2S（延迟到第一次响铃，节省配网和空闲时的内存与功耗），
// 播放后调用 bsp_audio_sleep() 让 codec 进入低功耗。
#pragma once

#include "esp_err.h"

esp_err_t codeloom_audio_start(void);
void codeloom_audio_chime(void);
