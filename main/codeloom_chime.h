// main/codeloom_chime.h — 新审批提示音合成（纯 C，16 bit 单声道 PCM）。
// 双音“叮-咚”：G5 → C6，每个音带线性起音和指数型衰减，总长约 420 ms。
// 按样本下标无状态生成，调用方可用任意小块缓冲分段写入 I2S。
#pragma once

#include <stddef.h>
#include <stdint.h>

#define CL_CHIME_SAMPLE_RATE 16000U
#define CL_CHIME_PEAK 9000

// 给定采样率下提示音的总样本数。
size_t cl_chime_total_samples(uint32_t sample_rate);

// 生成 [start, start + count) 范围内的样本；超出总长的部分填 0。
void cl_chime_fill(int16_t *out, size_t count, size_t start, uint32_t sample_rate);
