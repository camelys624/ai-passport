// main/fish_sound.c — 见 fish_sound.h。
//
// ESP32-C3 没有硬件浮点：逐样本调用 sinf()/expf() 会慢于实时，I2S 断流（声音残缺、
// 听起来很小），而且高优先级的音频任务会占满 CPU，把 LVGL 刷屏饿住。这里改为整数合成：
// 正弦与衰减曲线各用一张查找表（首次使用时建表一次），逐样本只做整数乘加与线性插值。
#include "fish_sound.h"

#include <math.h>
#include <stdbool.h>

typedef struct {
    uint16_t frequency_hz; // 0 = 静音间隔
    uint16_t duration_ms;
} note_t;

typedef struct {
    const note_t *notes;
    size_t count;
    int32_t peak; // 本音效的峰值幅度，≤ FISH_SOUND_PEAK
} melody_t;

// 小喇叭在 1 kHz 以下几乎放不出声（C5 附近的“柔和”版本在设备上听不到），而 2 kHz 以上
// 又显得尖。音高放在 G5–G6（784–1568 Hz）：纯正弦、缓起音，靠音色而不是低音高来显得柔和。
static const note_t BITE[] = {{1319, 220}, {0, 60}, {1047, 320}};
static const note_t CATCH[] = {{784, 110}, {1047, 110}, {1319, 110}, {1568, 260}};
static const note_t ESCAPE[] = {{1047, 150}, {880, 150}, {784, 280}};

// 各音效单独调响度：peak = FISH_SOUND_PEAK · 10^(dB/20)。
static const melody_t MELODIES[FISH_SOUND_COUNT] = {
    [FISH_SOUND_BITE] = {BITE, sizeof(BITE) / sizeof(BITE[0]), FISH_SOUND_PEAK},
    [FISH_SOUND_CATCH] = {CATCH, sizeof(CATCH) / sizeof(CATCH[0]), FISH_SOUND_PEAK * 891 / 1000}, // −1 dB
    [FISH_SOUND_ESCAPE] = {ESCAPE, sizeof(ESCAPE) / sizeof(ESCAPE[0]), FISH_SOUND_PEAK},
};

#define SINE_BITS 8U
#define SINE_SIZE (1U << SINE_BITS)
#define DECAY_SIZE 64U
#define Q15 32767
#define ATTACK_SAMPLES (FISH_SOUND_RATE * 15U / 1000U)  // 15 ms 缓起音，避免“啪”的起头
#define RELEASE_SAMPLES (FISH_SOUND_RATE * 20U / 1000U) // 20 ms 收尾
#define DECAY_RATE 1.2f                                  // 每个音按 e^(-1.2·进度) 衰减：有余韵，又保留足够能量

static int16_t s_sine[SINE_SIZE + 1];    // Q15，多一项便于插值时回绕
static int16_t s_decay[DECAY_SIZE + 1];  // Q15，e^(-DECAY_RATE·i/DECAY_SIZE)
static bool s_tables_ready;

static void build_tables(void)
{
    const float two_pi = 6.28318530718f;

    for (unsigned i = 0; i <= SINE_SIZE; ++i) {
        s_sine[i] = (int16_t)lrintf((float)Q15 * sinf(two_pi * (float)i / (float)SINE_SIZE));
    }
    for (unsigned i = 0; i <= DECAY_SIZE; ++i) {
        s_decay[i] = (int16_t)lrintf((float)Q15 * expf(-DECAY_RATE * (float)i / (float)DECAY_SIZE));
    }
    s_tables_ready = true;
}

static size_t note_samples(const note_t *note)
{
    return (size_t)FISH_SOUND_RATE * note->duration_ms / 1000U;
}

size_t fish_sound_samples(fish_sound_t sound)
{
    size_t total = 0;

    if ((unsigned)sound >= FISH_SOUND_COUNT) {
        return 0;
    }
    for (size_t i = 0; i < MELODIES[sound].count; ++i) {
        total += note_samples(&MELODIES[sound].notes[i]);
    }
    return total;
}

// 在 Q15 表中按 position/scale 线性插值；position < 表长 · scale，且 scale ≤ 65536。
// 全部 32 位整数运算（RV32 上 64 位除法是软件库调用）。
static int32_t lerp(const int16_t *table, uint32_t position, uint32_t scale)
{
    uint32_t i = position / scale;
    int32_t frac = (int32_t)(position % scale);
    return table[i] + (table[i + 1] - table[i]) * frac / (int32_t)scale;
}

static int16_t note_sample(const note_t *note, size_t index, size_t length, int32_t peak)
{
    // 周期内相位 = (index · f mod RATE) / RATE；乘 SINE_SIZE 后在正弦表上插值。
    // index · f ≤ 5120 · 1568（最长音 320 ms），(RATE − 1) · SINE_SIZE < 2^22，均不溢出 32 位。
    uint32_t phase = ((uint32_t)index * note->frequency_hz % FISH_SOUND_RATE) * SINE_SIZE;
    int32_t wave = lerp(s_sine, phase, FISH_SOUND_RATE);
    int32_t envelope = lerp(s_decay, (uint32_t)index * DECAY_SIZE * 1024U / (uint32_t)length, 1024U);

    if (index < ATTACK_SAMPLES) {
        envelope = envelope * (int32_t)index / (int32_t)ATTACK_SAMPLES;
    }
    if (length - index < RELEASE_SAMPLES) {
        envelope = envelope * (int32_t)(length - index) / (int32_t)RELEASE_SAMPLES;
    }
    return (int16_t)(((wave * envelope) >> 15) * peak >> 15);
}

static int16_t sample_at(const melody_t *melody, size_t index)
{
    for (size_t n = 0; n < melody->count; ++n) {
        const note_t *note = &melody->notes[n];
        size_t length = note_samples(note);
        if (index < length) {
            return note->frequency_hz == 0 ? 0 : note_sample(note, index, length, melody->peak);
        }
        index -= length;
    }
    return 0;
}

void fish_sound_fill(fish_sound_t sound, int16_t *out, size_t count, size_t start)
{
    if (out == NULL) {
        return;
    }
    if (!s_tables_ready) {
        build_tables();
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = (unsigned)sound < FISH_SOUND_COUNT ? sample_at(&MELODIES[sound], start + i) : 0;
    }
}
