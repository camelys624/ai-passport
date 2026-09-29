// main/fish_game.c — 见 fish_game.h。
#include "fish_game.h"

#include <string.h>

#define DEFAULT_SEED 0x2545F491U

// 回绕安全的“now 已到达 deadline”。
static bool reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint32_t until(uint32_t now_ms, uint32_t deadline_ms)
{
    return reached(now_ms, deadline_ms) ? 0U : deadline_ms - now_ms;
}

static uint32_t next_random(uint32_t *rng)
{
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

uint8_t fish_game_draw(uint32_t *rng, uint8_t bait)
{
    uint32_t total = 0;
    uint32_t pick;

    for (uint8_t i = 0; i < FISH_ENTRY_COUNT; ++i) {
        total += FISH_ENTRIES[i].weight[bait];
    }
    pick = next_random(rng) % total;
    for (uint8_t i = 0; i < FISH_ENTRY_COUNT; ++i) {
        uint32_t weight = FISH_ENTRIES[i].weight[bait];
        if (pick < weight) {
            return i;
        }
        pick -= weight;
    }
    return 0; // 不可达：pick < total
}

uint32_t fish_game_wait_ms(uint32_t *rng, uint8_t bait)
{
    const fish_bait_t *b = &FISH_BAITS[bait];
    return b->wait_min_ms + next_random(rng) % (b->wait_max_ms - b->wait_min_ms + 1U);
}

uint8_t fish_game_collected(const fg_progress_t *progress)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < FISH_ENTRY_COUNT; ++i) {
        n += progress->counts[i] > 0;
    }
    return n;
}

uint8_t fish_game_reel_run(const fish_game_t *game)
{
    uint8_t run = 0;

    if (game->view != FG_VIEW_REEL) {
        return 0;
    }
    for (uint8_t i = game->step_index; i < game->step_count; ++i) {
        if (game->steps[i] != game->steps[game->step_index]) {
            break;
        }
        ++run;
    }
    return run;
}

bool fish_game_reel_repeats(const fish_game_t *game)
{
    return game->view == FG_VIEW_REEL && game->step_index > 0 &&
           game->step_index < game->step_count &&
           game->steps[game->step_index] == game->steps[game->step_index - 1];
}

void fish_game_init(fish_game_t *game, const fg_progress_t *saved, uint32_t seed, uint32_t now_ms)
{
    memset(game, 0, sizeof(*game));
    if (saved != NULL) {
        game->progress = *saved;
    } else {
        game->progress.points = FG_DAILY_POINTS;
    }
    if (game->progress.bait >= FISH_BAIT_COUNT) {
        game->progress.bait = 0;
    }
    game->rng = seed != 0 ? seed : DEFAULT_SEED;
    game->view = FG_VIEW_READY;
    game->screen_on = true;
    game->last_activity_ms = now_ms;
    game->next_grant_ms = now_ms + FG_DAY_MS;
}

static uint32_t enter(fish_game_t *game, fg_view_t view)
{
    game->view = view;
    game->armed = 0;
    game->short_points = false;
    game->sell_qty = 0;
    return FG_FX_REDRAW;
}

static void add_points(fg_progress_t *p, uint32_t points)
{
    p->points = p->points > UINT32_MAX - points ? UINT32_MAX : p->points + points;
}

static uint32_t finish(fish_game_t *game, fg_outcome_t outcome, uint32_t now_ms)
{
    game->outcome = outcome;
    game->result_ms = now_ms;
    if (outcome == FG_OUTCOME_CAUGHT) {
        uint16_t *count = &game->progress.counts[game->entry];
        uint16_t *stock = &game->progress.stock[game->entry];
        if (*count < UINT16_MAX) {
            ++*count;
        }
        // 能卖钱的渔获放进鱼篓，到鱼册里卖；水草、旧靴子只记入鱼册。
        if (FISH_ENTRIES[game->entry].price > 0 && *stock < UINT16_MAX) {
            ++*stock;
        }
        return enter(game, FG_VIEW_RESULT) | FG_FX_SOUND_CATCH | FG_FX_SAVE;
    }
    if (game->progress.escapes < UINT32_MAX) {
        game->progress.escapes++;
    }
    return enter(game, FG_VIEW_RESULT) | FG_FX_SOUND_ESCAPE | FG_FX_SAVE;
}

static uint32_t cast(fish_game_t *game, uint32_t now_ms)
{
    uint16_t cost = FISH_BAITS[game->progress.bait].cost;

    if (game->progress.points < cost) {
        game->short_points = true;
        return FG_FX_UPDATE;
    }
    game->progress.points -= cost;
    if (game->progress.casts < UINT32_MAX) {
        game->progress.casts++;
    }
    game->cast_ms = now_ms;
    game->deadline_ms = now_ms + fish_game_wait_ms(&game->rng, game->progress.bait);
    // 扣掉的积分立即保存，断电不会把饵料退回来。
    return enter(game, FG_VIEW_WAITING) | FG_FX_SAVE;
}

static uint32_t start_reel(fish_game_t *game, uint32_t now_ms)
{
    uint8_t steps = FISH_ENTRIES[game->entry].reel_steps;

    game->step_count = steps < FG_REEL_MAX_STEPS ? steps : FG_REEL_MAX_STEPS;
    for (uint8_t i = 0; i < game->step_count; ++i) {
        game->steps[i] = (uint8_t)(next_random(&game->rng) % 3U);
    }
    game->step_index = 0;
    game->deadline_ms = now_ms + FG_REEL_STEP_MS;
    return enter(game, FG_VIEW_REEL);
}

static uint32_t reel_press(fish_game_t *game, fg_key_t key, uint32_t now_ms)
{
    if ((uint8_t)key != game->steps[game->step_index]) {
        // 提竿时习惯连按两下 OK：收线刚开始的一小段时间里，多出来的 OK 不算按错。
        uint32_t reel_start = game->deadline_ms - FG_REEL_STEP_MS;
        if (game->step_index == 0 && key == FG_KEY_OK &&
            (int32_t)(now_ms - reel_start) < (int32_t)FG_REEL_GRACE_MS) {
            return 0;
        }
        return finish(game, FG_OUTCOME_WRONG_KEY, now_ms);
    }
    if (++game->step_index == game->step_count) {
        return finish(game, FG_OUTCOME_CAUGHT, now_ms);
    }
    game->deadline_ms = now_ms + FG_REEL_STEP_MS;
    return FG_FX_UPDATE;
}

static uint32_t on_press(fish_game_t *game, fg_key_t key, uint32_t now_ms)
{
    game->armed |= (uint8_t)(1U << key);
    switch (game->view) {
    case FG_VIEW_READY:
        if (key == FG_KEY_UP) {
            game->progress.bait = (uint8_t)((game->progress.bait + FISH_BAIT_COUNT - 1U) % FISH_BAIT_COUNT);
            game->short_points = false;
            return FG_FX_UPDATE;
        }
        if (key == FG_KEY_DOWN) {
            game->progress.bait = (uint8_t)((game->progress.bait + 1U) % FISH_BAIT_COUNT);
            game->short_points = false;
            return FG_FX_UPDATE;
        }
        return 0;
    case FG_VIEW_BITE:
        return key == FG_KEY_OK ? start_reel(game, now_ms) : 0;
    case FG_VIEW_REEL:
        return reel_press(game, key, now_ms);
    case FG_VIEW_RESULT:
        // 按下即继续：连按时按键组件不报单击，不能依赖 CLICK。结果出现后，任何按键
        // 都会把“安静期”重新计时；手停下 FG_RESULT_GUARD_MS 后再按 OK 才继续，
        // 收线时的连按不会把结果页直接跳过。
        if ((int32_t)(now_ms - game->result_ms) < (int32_t)FG_RESULT_GUARD_MS) {
            game->result_ms = now_ms;
            return 0;
        }
        return key == FG_KEY_OK ? enter(game, FG_VIEW_READY) : 0;
    case FG_VIEW_ALBUM:
        if (key == FG_KEY_OK) {
            return 0; // OK 的单击卖鱼、长按退出，都等 CLICK/LONG
        }
        if (game->sell_qty > 0) {
            // 选卖出数量：▲ 加、▼ 减，在 1..持有数之间循环。
            uint16_t stock = game->progress.stock[game->album_index];
            if (key == FG_KEY_UP) {
                game->sell_qty = game->sell_qty >= stock ? 1 : (uint16_t)(game->sell_qty + 1U);
            } else {
                game->sell_qty = game->sell_qty <= 1 ? stock : (uint16_t)(game->sell_qty - 1U);
            }
            return FG_FX_UPDATE;
        }
        if (key == FG_KEY_UP) {
            game->album_index = (uint8_t)((game->album_index + FISH_ENTRY_COUNT - 1U) % FISH_ENTRY_COUNT);
        } else {
            game->album_index = (uint8_t)((game->album_index + 1U) % FISH_ENTRY_COUNT);
        }
        return FG_FX_UPDATE;
    default:
        return 0;
    }
}

static uint32_t album_click(fish_game_t *game)
{
    uint8_t i = game->album_index;
    uint16_t stock = game->progress.stock[i];

    if (game->sell_qty == 0) {
        if (stock == 0 || FISH_ENTRIES[i].price == 0) {
            return 0;
        }
        game->sell_qty = 1;
        return FG_FX_UPDATE;
    }
    game->progress.stock[i] = (uint16_t)(stock - game->sell_qty);
    add_points(&game->progress, (uint32_t)game->sell_qty * FISH_ENTRIES[i].price);
    game->sell_qty = 0;
    return FG_FX_UPDATE | FG_FX_SAVE | FG_FX_SOUND_CATCH;
}

static uint32_t on_click(fish_game_t *game, fg_key_t key, uint32_t now_ms)
{
    if (key != FG_KEY_OK) {
        return 0;
    }
    switch (game->view) {
    case FG_VIEW_READY:
        return cast(game, now_ms);
    case FG_VIEW_ALBUM:
        return album_click(game);
    default:
        return 0;
    }
}

static uint32_t on_long(fish_game_t *game, fg_key_t key)
{
    if (key != FG_KEY_OK) {
        return 0;
    }
    switch (game->view) {
    case FG_VIEW_READY:
        game->album_index = 0;
        return enter(game, FG_VIEW_ALBUM);
    case FG_VIEW_WAITING:
        // 收竿：饵料已消耗，不退积分；抛竿时已保存。
        return enter(game, FG_VIEW_READY);
    case FG_VIEW_ALBUM:
        // 选数量时长按取消卖出，否则退出鱼册。
        if (game->sell_qty > 0) {
            game->sell_qty = 0;
            return FG_FX_UPDATE;
        }
        return enter(game, FG_VIEW_READY);
    default:
        return 0;
    }
}

uint32_t fish_game_input(fish_game_t *game, fg_key_t key, fg_input_t input, uint32_t now_ms)
{
    if (key > FG_KEY_OK) {
        return 0;
    }
    if (!game->screen_on) {
        if (input != FG_INPUT_PRESS) {
            return 0;
        }
        // 第一次按下只亮屏；清掉 armed，这次按键的 CLICK/LONG 也不会生效。
        game->screen_on = true;
        game->armed = 0;
        game->last_activity_ms = now_ms;
        return FG_FX_SCREEN_ON;
    }
    game->last_activity_ms = now_ms;
    if (input == FG_INPUT_PRESS) {
        return on_press(game, key, now_ms);
    }
    if ((game->armed & (1U << key)) == 0) {
        return 0;
    }
    return input == FG_INPUT_CLICK ? on_click(game, key, now_ms) : on_long(game, key);
}

static uint32_t dim_limit(const fish_game_t *game)
{
    switch (game->view) {
    case FG_VIEW_WAITING:
        return FG_WAIT_DIM_MS;
    case FG_VIEW_BITE:
    case FG_VIEW_REEL:
        return FG_NO_DEADLINE;
    default:
        return FG_IDLE_DIM_MS;
    }
}

// 每开机运行满 FG_DAY_MS 送 FG_DAILY_POINTS。
static uint32_t tick_day(fish_game_t *game, uint32_t now_ms)
{
    uint32_t fx = 0;

    while (reached(now_ms, game->next_grant_ms)) {
        game->next_grant_ms += FG_DAY_MS;
        add_points(&game->progress, FG_DAILY_POINTS);
        fx |= FG_FX_SAVE | FG_FX_UPDATE;
    }
    return fx;
}

static uint32_t tick_play(fish_game_t *game, uint32_t now_ms)
{
    uint32_t fx = 0;
    uint32_t dim;

    switch (game->view) {
    case FG_VIEW_WAITING:
        if (reached(now_ms, game->deadline_ms)) {
            game->entry = fish_game_draw(&game->rng, game->progress.bait);
            game->deadline_ms = now_ms + FG_BITE_WINDOW_MS;
            game->next_beep_ms = now_ms + FG_BITE_BEEP_INTERVAL_MS;
            game->last_activity_ms = now_ms;
            fx |= enter(game, FG_VIEW_BITE) | FG_FX_SOUND_BITE;
            if (!game->screen_on) {
                game->screen_on = true;
                fx |= FG_FX_SCREEN_ON;
            }
            return fx;
        }
        break;
    case FG_VIEW_BITE:
        if (reached(now_ms, game->deadline_ms)) {
            return finish(game, FG_OUTCOME_MISSED, now_ms);
        }
        if (reached(now_ms, game->next_beep_ms)) {
            game->next_beep_ms += FG_BITE_BEEP_INTERVAL_MS;
            return FG_FX_SOUND_BITE;
        }
        return 0;
    case FG_VIEW_REEL:
        if (reached(now_ms, game->deadline_ms)) {
            return finish(game, FG_OUTCOME_TOO_SLOW, now_ms);
        }
        return 0;
    default:
        break;
    }

    dim = dim_limit(game);
    if (game->screen_on && dim != FG_NO_DEADLINE && reached(now_ms, game->last_activity_ms + dim)) {
        game->screen_on = false;
        fx |= FG_FX_SCREEN_OFF;
    }
    return fx;
}

uint32_t fish_game_tick(fish_game_t *game, uint32_t now_ms)
{
    uint32_t fx = tick_day(game, now_ms);
    return fx | tick_play(game, now_ms);
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

uint32_t fish_game_ms_until_next(const fish_game_t *game, uint32_t now_ms)
{
    uint32_t next = FG_NO_DEADLINE;
    uint32_t dim = dim_limit(game);

    switch (game->view) {
    case FG_VIEW_WAITING:
    case FG_VIEW_REEL:
        next = until(now_ms, game->deadline_ms);
        break;
    case FG_VIEW_BITE:
        next = min_u32(until(now_ms, game->deadline_ms), until(now_ms, game->next_beep_ms));
        break;
    default:
        break;
    }
    if (game->screen_on && dim != FG_NO_DEADLINE) {
        next = min_u32(next, until(now_ms, game->last_activity_ms + dim));
    }
    return min_u32(next, until(now_ms, game->next_grant_ms));
}
