// tests/test_fish_sound.c — 整数提示音合成的主机测试：音高、幅度上限、静音间隔、分块一致。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fish_sound.h"

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                                  \
        }                                                                             \
    } while (0)

// 在 [from, to) 内数过零次数，换算成频率（Hz）。
static int measured_hz(const int16_t *pcm, size_t from, size_t to)
{
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i) {
        crossings += (pcm[i - 1] < 0) != (pcm[i] < 0);
    }
    return (int)((long)crossings * FISH_SOUND_RATE / 2 / (long)(to - from));
}

static int16_t *render(fish_sound_t sound, size_t *count)
{
    *count = fish_sound_samples(sound);
    int16_t *pcm = malloc(*count * sizeof(*pcm));
    CHECK(pcm != NULL);
    fish_sound_fill(sound, pcm, *count, 0);
    return pcm;
}

int main(void)
{
    // 每个音效第一个音的音高（取前 100 ms）。
    static const int FIRST_HZ[FISH_SOUND_COUNT] = {1319, 784, 1047};
    const size_t window = FISH_SOUND_RATE / 10;

    for (int s = 0; s < FISH_SOUND_COUNT; ++s) {
        size_t count;
        int16_t *pcm = render((fish_sound_t)s, &count);
        int peak = 0;

        CHECK(count > window);
        for (size_t i = 0; i < count; ++i) {
            int v = abs(pcm[i]);
            peak = v > peak ? v : peak;
        }
        CHECK(peak <= FISH_SOUND_PEAK);
        CHECK(peak > FISH_SOUND_PEAK / 2); // 不是静音或被错误缩放

        int hz = measured_hz(pcm, 0, window);
        CHECK(abs(hz - FIRST_HZ[s]) <= FIRST_HZ[s] / 50);

        // 板载小喇叭在 1 kHz 以下几乎不出声：整段有声部分的平均音高不能掉到 750 Hz 以下。
        size_t voiced = 0;
        int crossings = 0;
        for (size_t i = 1; i < count; ++i) {
            if (pcm[i] != 0 || pcm[i - 1] != 0) {
                ++voiced;
                crossings += (pcm[i - 1] < 0) != (pcm[i] < 0);
            }
        }
        CHECK((long)crossings * FISH_SOUND_RATE / 2 / (long)voiced >= 750);

        // 分块生成与整段生成逐样本一致（按下标无状态）。
        int16_t chunk[97];
        for (size_t start = 0; start < count; start += sizeof(chunk) / sizeof(chunk[0])) {
            size_t n = count - start < 97 ? count - start : 97;
            fish_sound_fill((fish_sound_t)s, chunk, n, start);
            CHECK(memcmp(chunk, pcm + start, n * sizeof(chunk[0])) == 0);
        }
        free(pcm);
    }

    // 咬钩音两音之间的 80 ms 间隔是静音；第二个音是 C5。
    size_t count;
    int16_t *bite = render(FISH_SOUND_BITE, &count);
    size_t gap_start = FISH_SOUND_RATE * 220 / 1000, gap_end = FISH_SOUND_RATE * 280 / 1000;
    for (size_t i = gap_start; i < gap_end; ++i) {
        CHECK(bite[i] == 0);
    }
    CHECK(abs(measured_hz(bite, gap_end, gap_end + window) - 1047) <= 1047 / 50);
    free(bite);

    // 越界请求填 0。
    int16_t tail[4] = {1, 1, 1, 1};
    fish_sound_fill(FISH_SOUND_CATCH, tail, 4, fish_sound_samples(FISH_SOUND_CATCH));
    CHECK(tail[0] == 0 && tail[3] == 0);

    puts("test_fish_sound: PASS");
    return 0;
}
