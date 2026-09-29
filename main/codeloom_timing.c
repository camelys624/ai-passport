// main/codeloom_timing.c — 见 codeloom_timing.h。
#include "codeloom_timing.h"

#include <stddef.h>

void cl_backlight_init(cl_backlight_t *bl, uint64_t now_ms)
{
    bl->screen = CL_SCREEN_ON;
    bl->last_activity_ms = now_ms;
    bl->swallow_button = -1;
    bl->swallow_until_ms = 0;
}

bool cl_backlight_press(cl_backlight_t *bl, int button, uint64_t now_ms)
{
    bool woke = bl->screen != CL_SCREEN_ON;

    bl->last_activity_ms = now_ms;
    if (woke) {
        bl->screen = CL_SCREEN_ON;
        bl->swallow_button = button;
        bl->swallow_until_ms = now_ms + CL_WAKE_SWALLOW_MS;
    }
    return woke;
}

bool cl_backlight_gesture_swallowed(cl_backlight_t *bl, int button, uint64_t now_ms,
                                    bool *screen_changed)
{
    bool changed = false;
    bool swallowed = false;

    if (bl->swallow_button >= 0 && now_ms > bl->swallow_until_ms) {
        bl->swallow_button = -1;
    }
    if (bl->swallow_button == button) {
        bl->swallow_button = -1;
        swallowed = true;
    } else if (bl->screen != CL_SCREEN_ON) {
        bl->screen = CL_SCREEN_ON;
        changed = true;
        swallowed = true;
    }
    bl->last_activity_ms = now_ms;
    if (screen_changed != NULL) {
        *screen_changed = changed;
    }
    return swallowed;
}

bool cl_backlight_wake(cl_backlight_t *bl, uint64_t now_ms)
{
    bool changed = bl->screen != CL_SCREEN_ON;

    bl->screen = CL_SCREEN_ON;
    bl->last_activity_ms = now_ms;
    return changed;
}

bool cl_backlight_tick(cl_backlight_t *bl, uint64_t now_ms)
{
    uint64_t idle = now_ms >= bl->last_activity_ms ? now_ms - bl->last_activity_ms : 0;
    cl_screen_t next = CL_SCREEN_ON;

    if (idle >= CL_BACKLIGHT_OFF_AFTER_MS) {
        next = CL_SCREEN_OFF;
    } else if (idle >= CL_BACKLIGHT_DIM_AFTER_MS) {
        next = CL_SCREEN_DIM;
    }
    if (next == bl->screen) {
        return false;
    }
    bl->screen = next;
    return true;
}

uint8_t cl_backlight_percent(cl_screen_t screen)
{
    switch (screen) {
    case CL_SCREEN_ON:
        return CL_BACKLIGHT_ON_PERCENT;
    case CL_SCREEN_DIM:
        return CL_BACKLIGHT_DIM_PERCENT;
    default:
        return 0;
    }
}

static uint32_t exponential(uint32_t base, unsigned steps, uint32_t cap)
{
    uint64_t value = base;

    while (steps-- > 0 && value < cap) {
        value *= 2U;
    }
    return value > cap ? cap : (uint32_t)value;
}

uint32_t cl_poll_delay_ms(bool screen_off, unsigned failures, bool revoked)
{
    uint32_t base = screen_off ? CL_POLL_SCREEN_OFF_MS : CL_POLL_SCREEN_ON_MS;

    if (revoked) {
        return CL_POLL_MAX_MS;
    }
    return exponential(base, failures, CL_POLL_MAX_MS);
}

uint32_t cl_wifi_retry_delay_ms(unsigned attempt)
{
    return exponential(CL_WIFI_RETRY_BASE_MS, attempt, CL_WIFI_RETRY_MAX_MS);
}
