// main/fish_save.c — 见 fish_save.h。
#include "fish_save.h"

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void fish_save_encode(const fg_progress_t *progress, uint8_t out[FISH_SAVE_SIZE])
{
    out[0] = 'F';
    out[1] = 'S';
    out[2] = FISH_SAVE_VERSION;
    out[3] = progress->bait;
    put_u32(out + 4, progress->casts);
    put_u32(out + 8, progress->escapes);
    for (unsigned i = 0; i < FISH_ENTRY_MAX; ++i) {
        put_u16(out + 12 + 2 * i, progress->counts[i]);
    }
}

bool fish_save_decode(const uint8_t *data, size_t length, fg_progress_t *out)
{
    if (data == NULL || length != FISH_SAVE_SIZE || data[0] != 'F' || data[1] != 'S' ||
        data[2] != FISH_SAVE_VERSION) {
        return false;
    }
    out->bait = data[3];
    out->casts = get_u32(data + 4);
    out->escapes = get_u32(data + 8);
    for (unsigned i = 0; i < FISH_ENTRY_MAX; ++i) {
        out->counts[i] = get_u16(data + 12 + 2 * i);
    }
    return true;
}
