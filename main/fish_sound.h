// main/fish_sound.h — 提示音合成（纯 C，16 bit 单声道 PCM，16 kHz）。
// 柔和取向：纯正弦、缓起音 + 指数衰减，音高在 G5–G6（784–1568 Hz）——板载小喇叭在 1 kHz
// 以下几乎不出声，更高又显得尖。整数合成（查表 + 线性插值），逐样本开销很小，不会拖慢刷屏。
// 整体音量再由 fish_audio.c 的 codec 音量控制。
// 按样本下标无状态生成，调用方可用小块缓冲分段写入 I2S。
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FISH_SOUND_RATE 16000U
#define FISH_SOUND_PEAK 12000

typedef enum {
    FISH_SOUND_BITE = 0,   // 咬钩“叮—咚”（E6 → C6）
    FISH_SOUND_CATCH,      // 钓到：上行琶音
    FISH_SOUND_ESCAPE,     // 跑掉：下行三音
    FISH_SOUND_COUNT,
} fish_sound_t;

size_t fish_sound_samples(fish_sound_t sound);

// 生成 [start, start + count) 范围内的样本；超出总长的部分填 0。
void fish_sound_fill(fish_sound_t sound, int16_t *out, size_t count, size_t start);
