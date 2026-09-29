// main/codeloom_chime.c — 见 codeloom_chime.h。
#include "codeloom_chime.h"

#include <math.h>

typedef struct {
    float frequency;
    uint32_t duration_ms;
} chime_note_t;

static const chime_note_t NOTES[] = {
    {783.99f, 160U},  // G5
    {1046.50f, 260U}, // C6
};

#define NOTE_COUNT (sizeof(NOTES) / sizeof(NOTES[0]))
#define ATTACK_MS 6U

static size_t note_samples(const chime_note_t *note, uint32_t sample_rate)
{
    return (size_t)((uint64_t)sample_rate * note->duration_ms / 1000U);
}

size_t cl_chime_total_samples(uint32_t sample_rate)
{
    size_t total = 0;

    for (size_t i = 0; i < NOTE_COUNT; ++i) {
        total += note_samples(&NOTES[i], sample_rate);
    }
    return total;
}

static int16_t sample_at(size_t index, uint32_t sample_rate)
{
    const float two_pi = 6.28318530718f;

    for (size_t n = 0; n < NOTE_COUNT; ++n) {
        size_t length = note_samples(&NOTES[n], sample_rate);
        if (index < length) {
            float t = (float)index / (float)sample_rate;
            float progress = (float)index / (float)length;
            float attack_samples = (float)sample_rate * ATTACK_MS / 1000.0f;
            float envelope = (1.0f - progress) * (1.0f - progress);
            if ((float)index < attack_samples) {
                envelope *= (float)index / attack_samples;
            }
            // 加一点二次谐波让音色更像铃声，同时保持峰值不超过 CL_CHIME_PEAK。
            float wave = 0.8f * sinf(two_pi * NOTES[n].frequency * t) +
                         0.2f * sinf(two_pi * 2.0f * NOTES[n].frequency * t);
            return (int16_t)(wave * envelope * (float)CL_CHIME_PEAK);
        }
        index -= length;
    }
    return 0;
}

void cl_chime_fill(int16_t *out, size_t count, size_t start, uint32_t sample_rate)
{
    if (out == NULL || sample_rate == 0) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = sample_at(start + i, sample_rate);
    }
}
