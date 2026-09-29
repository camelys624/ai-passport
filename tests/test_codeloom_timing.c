// Host tests for backlight idle policy, wake-press swallowing, poll/reconnect backoff,
// and the synthesized chime.
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "codeloom_chime.h"
#include "codeloom_timing.h"

static void test_backlight_idle(void)
{
    cl_backlight_t bl;

    cl_backlight_init(&bl, 1000);
    assert(bl.screen == CL_SCREEN_ON);
    assert(!cl_backlight_tick(&bl, 1000 + CL_BACKLIGHT_DIM_AFTER_MS - 1));
    assert(cl_backlight_tick(&bl, 1000 + CL_BACKLIGHT_DIM_AFTER_MS));
    assert(bl.screen == CL_SCREEN_DIM);
    assert(!cl_backlight_tick(&bl, 1000 + CL_BACKLIGHT_DIM_AFTER_MS + 5));
    assert(cl_backlight_tick(&bl, 1000 + CL_BACKLIGHT_OFF_AFTER_MS));
    assert(bl.screen == CL_SCREEN_OFF);
    assert(cl_backlight_percent(CL_SCREEN_ON) == CL_BACKLIGHT_ON_PERCENT);
    assert(cl_backlight_percent(CL_SCREEN_DIM) == CL_BACKLIGHT_DIM_PERCENT);
    assert(cl_backlight_percent(CL_SCREEN_OFF) == 0);

    // Alert wake restarts the idle timer.
    assert(cl_backlight_wake(&bl, 100000));
    assert(bl.screen == CL_SCREEN_ON);
    assert(!cl_backlight_wake(&bl, 100001));
    assert(!cl_backlight_tick(&bl, 100000 + CL_BACKLIGHT_DIM_AFTER_MS - 1));
}

static void test_first_press_only_wakes(void)
{
    cl_backlight_t bl;
    bool changed;

    cl_backlight_init(&bl, 0);
    // Active screen: presses and gestures pass through.
    assert(!cl_backlight_press(&bl, 2, 10));
    assert(!cl_backlight_gesture_swallowed(&bl, 2, 20, &changed) && !changed);

    cl_backlight_tick(&bl, 20 + CL_BACKLIGHT_OFF_AFTER_MS);
    assert(bl.screen == CL_SCREEN_OFF);
    // Press wakes; the gesture of that same press is swallowed exactly once.
    assert(cl_backlight_press(&bl, 2, 100000));
    assert(bl.screen == CL_SCREEN_ON);
    assert(cl_backlight_gesture_swallowed(&bl, 2, 100300, &changed) && !changed);
    assert(!cl_backlight_gesture_swallowed(&bl, 2, 100900, &changed));

    // A different button's gesture is not swallowed.
    cl_backlight_tick(&bl, 100900 + CL_BACKLIGHT_DIM_AFTER_MS);
    assert(bl.screen == CL_SCREEN_DIM);
    assert(cl_backlight_press(&bl, 0, 200000));
    assert(!cl_backlight_gesture_swallowed(&bl, 1, 200100, &changed));

    // Stale swallow marker (no gesture followed the wake press) expires.
    cl_backlight_tick(&bl, 200100 + CL_BACKLIGHT_OFF_AFTER_MS);
    assert(cl_backlight_press(&bl, 1, 400000));
    assert(!cl_backlight_gesture_swallowed(&bl, 1, 400000 + CL_WAKE_SWALLOW_MS + 1, &changed));

    // Gesture without a preceding press still only wakes.
    cl_backlight_tick(&bl, 500000 + CL_BACKLIGHT_OFF_AFTER_MS);
    assert(cl_backlight_gesture_swallowed(&bl, 0, 600000, &changed) && changed);
    assert(bl.screen == CL_SCREEN_ON);
}

static void test_poll_delays(void)
{
    assert(cl_poll_delay_ms(false, 0, false) == 5000);
    assert(cl_poll_delay_ms(true, 0, false) == 20000);
    assert(cl_poll_delay_ms(false, 1, false) == 10000);
    assert(cl_poll_delay_ms(false, 3, false) == 40000);
    assert(cl_poll_delay_ms(false, 4, false) == CL_POLL_MAX_MS);
    assert(cl_poll_delay_ms(false, 1000, false) == CL_POLL_MAX_MS);
    assert(cl_poll_delay_ms(true, 2, false) == CL_POLL_MAX_MS);
    assert(cl_poll_delay_ms(false, 0, true) == CL_POLL_MAX_MS);

    assert(cl_wifi_retry_delay_ms(0) == 1000);
    assert(cl_wifi_retry_delay_ms(1) == 2000);
    assert(cl_wifi_retry_delay_ms(5) == 32000);
    assert(cl_wifi_retry_delay_ms(6) == CL_WIFI_RETRY_MAX_MS);
    assert(cl_wifi_retry_delay_ms(4000000000U) == CL_WIFI_RETRY_MAX_MS);
}

static void test_chime(void)
{
    size_t total = cl_chime_total_samples(CL_CHIME_SAMPLE_RATE);
    int16_t *whole = calloc(total + 16, sizeof(int16_t));
    int16_t chunk[100];
    int peak = 0;

    assert(total == CL_CHIME_SAMPLE_RATE * 420 / 1000);
    assert(whole != NULL);
    cl_chime_fill(whole, total + 16, 0, CL_CHIME_SAMPLE_RATE);
    for (size_t i = 0; i < total; ++i) {
        int v = abs(whole[i]);
        peak = v > peak ? v : peak;
    }
    assert(peak > CL_CHIME_PEAK / 4 && peak <= CL_CHIME_PEAK);
    assert(whole[0] == 0);
    for (size_t i = total; i < total + 16; ++i) {
        assert(whole[i] == 0); // silence past the end
    }
    // Chunked generation matches the one-shot rendering.
    for (size_t start = 0; start < total; start += 100) {
        cl_chime_fill(chunk, 100, start, CL_CHIME_SAMPLE_RATE);
        for (size_t i = 0; i < 100 && start + i < total; ++i) {
            assert(chunk[i] == whole[start + i]);
        }
    }
    free(whole);
}

int main(void)
{
    test_backlight_idle();
    test_first_press_only_wakes();
    test_poll_delays();
    test_chime();
    return 0;
}
