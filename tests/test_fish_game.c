// tests/test_fish_game.c — 钓鱼状态机、按饵料抽取与存档编码的主机测试。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fish_game.h"
#include "fish_save.h"

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                                  \
        }                                                                             \
    } while (0)

static uint32_t tap(fish_game_t *g, fg_key_t key, uint32_t now)
{
    return fish_game_input(g, key, FG_INPUT_PRESS, now) | fish_game_input(g, key, FG_INPUT_CLICK, now);
}

// 从准备页抛竿并推进到咬钩，返回咬钩时刻。
static uint32_t cast_until_bite(fish_game_t *g, uint32_t now)
{
    CHECK(g->view == FG_VIEW_READY);
    tap(g, FG_KEY_OK, now);
    CHECK(g->view == FG_VIEW_WAITING);
    uint32_t bite = g->deadline_ms;
    fish_game_tick(g, bite);
    CHECK(g->view == FG_VIEW_BITE);
    return bite;
}

static void test_draw_follows_bait_weights(void)
{
    enum { DRAWS = 200000 };
    for (uint8_t bait = 0; bait < FISH_BAIT_COUNT; ++bait) {
        uint32_t rng = 12345U + bait;
        uint32_t hits[FISH_ENTRY_COUNT] = {0};
        uint32_t total = 0;
        for (int i = 0; i < FISH_ENTRY_COUNT; ++i) {
            total += FISH_ENTRIES[i].weight[bait];
        }
        for (int i = 0; i < DRAWS; ++i) {
            uint8_t entry = fish_game_draw(&rng, bait);
            CHECK(entry < FISH_ENTRY_COUNT);
            hits[entry]++;
        }
        for (int i = 0; i < FISH_ENTRY_COUNT; ++i) {
            double expected = (double)FISH_ENTRIES[i].weight[bait] / total;
            double observed = (double)hits[i] / DRAWS;
            if (FISH_ENTRIES[i].weight[bait] == 0) {
                CHECK(hits[i] == 0);
            }
            CHECK(observed > expected - 0.01 && observed < expected + 0.01);
        }
    }
}

static void test_wait_stays_in_bait_range(void)
{
    for (uint8_t bait = 0; bait < FISH_BAIT_COUNT; ++bait) {
        uint32_t rng = 99U;
        uint32_t lo = UINT32_MAX, hi = 0;
        for (int i = 0; i < 20000; ++i) {
            uint32_t w = fish_game_wait_ms(&rng, bait);
            lo = w < lo ? w : lo;
            hi = w > hi ? w : hi;
        }
        CHECK(lo >= FISH_BAITS[bait].wait_min_ms && hi <= FISH_BAITS[bait].wait_max_ms);
        CHECK(hi - lo > (FISH_BAITS[bait].wait_max_ms - FISH_BAITS[bait].wait_min_ms) * 9 / 10);
    }
}

static void test_catalog_is_playable(void)
{
    for (uint8_t bait = 0; bait < FISH_BAIT_COUNT; ++bait) {
        uint32_t total = 0;
        for (int i = 0; i < FISH_ENTRY_COUNT; ++i) {
            total += FISH_ENTRIES[i].weight[bait];
        }
        CHECK(total > 0);
        CHECK(FISH_BAITS[bait].wait_min_ms <= FISH_BAITS[bait].wait_max_ms);
    }
    for (int i = 0; i < FISH_ENTRY_COUNT; ++i) {
        CHECK(FISH_ENTRIES[i].reel_steps >= 1 && FISH_ENTRIES[i].reel_steps <= FG_REEL_MAX_STEPS);
    }
}

static void test_bait_selection_and_cast(void)
{
    fish_game_t g;
    fg_progress_t saved = {.bait = 2, .points = 100};
    fish_game_init(&g, &saved, 7, 1000);
    CHECK(g.progress.bait == 2);

    CHECK(fish_game_input(&g, FG_KEY_DOWN, FG_INPUT_PRESS, 1000) == FG_FX_UPDATE);
    CHECK(g.progress.bait == 0);
    CHECK(fish_game_input(&g, FG_KEY_UP, FG_INPUT_PRESS, 1000) == FG_FX_UPDATE);
    CHECK(g.progress.bait == 2);
    fish_game_input(&g, FG_KEY_UP, FG_INPUT_PRESS, 1000);
    CHECK(g.progress.bait == 1);

    CHECK(tap(&g, FG_KEY_OK, 2000) == (FG_FX_REDRAW | FG_FX_SAVE));
    CHECK(g.view == FG_VIEW_WAITING);
    CHECK(g.progress.casts == 1);
    CHECK(g.progress.points == 100U - FISH_BAITS[1].cost);
    CHECK(g.deadline_ms - 2000 >= FISH_BAITS[1].wait_min_ms);
    CHECK(g.deadline_ms - 2000 <= FISH_BAITS[1].wait_max_ms);

    // 等待中换饵键无效。
    fish_game_input(&g, FG_KEY_DOWN, FG_INPUT_PRESS, 2100);
    CHECK(g.progress.bait == 1);

    fish_game_t bad;
    fg_progress_t corrupt = {.bait = FISH_BAIT_COUNT};
    fish_game_init(&bad, &corrupt, 1, 0);
    CHECK(bad.progress.bait == 0);
}

static void test_click_needs_press_in_same_view(void)
{
    fish_game_t g;
    fish_game_init(&g, NULL, 7, 0);
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_CLICK, 10) == 0);
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_LONG, 10) == 0);
    CHECK(g.view == FG_VIEW_READY);

    // 长按 OK 进鱼册；同一次按压抬起后的事件不会把鱼册关掉。
    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, 20);
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_LONG, 1520) & FG_FX_REDRAW);
    CHECK(g.view == FG_VIEW_ALBUM);
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_CLICK, 1600) == 0);
    CHECK(g.view == FG_VIEW_ALBUM);

    CHECK(fish_game_input(&g, FG_KEY_UP, FG_INPUT_PRESS, 1700) == FG_FX_UPDATE);
    CHECK(g.album_index == FISH_ENTRY_COUNT - 1);
    fish_game_input(&g, FG_KEY_DOWN, FG_INPUT_PRESS, 1800);
    CHECK(g.album_index == 0);
    tap(&g, FG_KEY_OK, 1900);
    CHECK(g.view == FG_VIEW_READY);
}

static void test_waiting_dims_and_first_press_only_wakes(void)
{
    fish_game_t g;
    fish_game_init(&g, NULL, 7, 0);
    tap(&g, FG_KEY_OK, 100);
    CHECK(g.view == FG_VIEW_WAITING);
    CHECK(fish_game_ms_until_next(&g, 100) == FG_WAIT_DIM_MS);

    CHECK(fish_game_tick(&g, 100 + FG_WAIT_DIM_MS - 1) == 0);
    CHECK(fish_game_tick(&g, 100 + FG_WAIT_DIM_MS) == FG_FX_SCREEN_OFF);
    CHECK(!g.screen_on);
    CHECK(fish_game_ms_until_next(&g, 100 + FG_WAIT_DIM_MS) == g.deadline_ms - (100 + FG_WAIT_DIM_MS));

    // 黑屏时长按 OK：按下只亮屏，这次长按不会收竿。
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, 8000) == FG_FX_SCREEN_ON);
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_LONG, 9500) == 0);
    CHECK(g.view == FG_VIEW_WAITING && g.screen_on);

    // 再次长按才收竿；饵料已在抛竿时扣掉并保存，收竿不退积分。
    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, 10000);
    uint32_t fx = fish_game_input(&g, FG_KEY_OK, FG_INPUT_LONG, 11500);
    CHECK(fx == FG_FX_REDRAW);
    CHECK(g.progress.points == FG_DAILY_POINTS - FISH_BAITS[0].cost);
    CHECK(g.view == FG_VIEW_READY);
    CHECK(g.progress.casts == 1);
}

static void test_bite_wakes_beeps_and_escapes(void)
{
    fish_game_t g;
    fish_game_init(&g, NULL, 7, 0);
    tap(&g, FG_KEY_OK, 0);
    fish_game_tick(&g, FG_WAIT_DIM_MS);
    CHECK(!g.screen_on);

    uint32_t bite = g.deadline_ms;
    CHECK(fish_game_tick(&g, bite - 1) == 0);
    uint32_t fx = fish_game_tick(&g, bite);
    CHECK(fx == (FG_FX_REDRAW | FG_FX_SOUND_BITE | FG_FX_SCREEN_ON));
    CHECK(g.view == FG_VIEW_BITE && g.screen_on);
    CHECK(g.entry < FISH_ENTRY_COUNT);

    CHECK(fish_game_tick(&g, bite + FG_BITE_BEEP_INTERVAL_MS - 1) == 0);
    CHECK(fish_game_tick(&g, bite + FG_BITE_BEEP_INTERVAL_MS) == FG_FX_SOUND_BITE);
    CHECK(fish_game_tick(&g, bite + 2 * FG_BITE_BEEP_INTERVAL_MS) == FG_FX_SOUND_BITE);
    // 咬钩期间 UP/DOWN 不算提竿。
    CHECK(fish_game_input(&g, FG_KEY_UP, FG_INPUT_PRESS, bite + 5000) == 0);
    CHECK(fish_game_tick(&g, bite + FG_BITE_WINDOW_MS - 1) == 0);

    fx = fish_game_tick(&g, bite + FG_BITE_WINDOW_MS);
    CHECK(fx == (FG_FX_REDRAW | FG_FX_SOUND_ESCAPE | FG_FX_SAVE));
    CHECK(g.view == FG_VIEW_RESULT && g.outcome == FG_OUTCOME_MISSED);
    CHECK(g.progress.escapes == 1);
    CHECK(fish_game_collected(&g.progress) == 0);
}

static void test_reel_success_records_catch(void)
{
    fish_game_t g;
    fish_game_init(&g, NULL, 11, 0);
    uint32_t now = cast_until_bite(&g, 0) + 800;

    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now) & FG_FX_REDRAW);
    CHECK(g.view == FG_VIEW_REEL);
    CHECK(g.step_count == FISH_ENTRIES[g.entry].reel_steps);
    // 提竿那次按键抬起的 CLICK 不算收线输入。
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_CLICK, now + 50) == 0);
    CHECK(g.step_index == 0);

    uint32_t fx = 0;
    for (uint8_t i = 0; i < g.step_count; ++i) {
        now += FG_REEL_STEP_MS - 1;
        CHECK(fish_game_tick(&g, now) == 0);
        fx = fish_game_input(&g, (fg_key_t)g.steps[i], FG_INPUT_PRESS, now);
    }
    CHECK(fx == (FG_FX_REDRAW | FG_FX_SOUND_CATCH | FG_FX_SAVE));
    CHECK(g.view == FG_VIEW_RESULT && g.outcome == FG_OUTCOME_CAUGHT);
    CHECK(g.progress.counts[g.entry] == 1);
    CHECK(fish_game_collected(&g.progress) == 1);

    // 连按收线：结果页出现后一直连按（间隔短于安静期）永远不会跳过结果页。
    fish_game_input(&g, (fg_key_t)g.steps[g.step_count - 1], FG_INPUT_CLICK, now + 50);
    uint32_t t = now;
    for (int i = 0; i < 10; ++i) {
        t += FG_RESULT_GUARD_MS - 1;
        CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, t) == 0);
        CHECK(g.view == FG_VIEW_RESULT);
    }
    // 停手满安静期后：非 OK 键不继续，OK 按下即继续（不依赖单击）。
    t += FG_RESULT_GUARD_MS;
    CHECK(fish_game_input(&g, FG_KEY_UP, FG_INPUT_PRESS, t) == 0);
    CHECK(g.view == FG_VIEW_RESULT);
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, t) & FG_FX_REDRAW);
    CHECK(g.view == FG_VIEW_READY);
    // 继续那次按下的抬起不会在准备页触发抛竿。
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_CLICK, t + 100) == 0);
    CHECK(g.view == FG_VIEW_READY);
}

static void test_reel_run_hint(void)
{
    fish_game_t g;
    fish_game_init(&g, NULL, 3, 0);
    uint32_t now = cast_until_bite(&g, 0);
    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now);
    CHECK(g.view == FG_VIEW_REEL);
    // 固定一组步骤：▼ ▼ ▼ OK ▲（只测提示计算，不改变步数）。
    static const uint8_t steps[] = {FG_KEY_DOWN, FG_KEY_DOWN, FG_KEY_DOWN, FG_KEY_OK, FG_KEY_UP};
    g.step_count = 5;
    for (int i = 0; i < 5; ++i) {
        g.steps[i] = steps[i];
    }
    CHECK(fish_game_reel_run(&g) == 3 && !fish_game_reel_repeats(&g));
    fish_game_input(&g, FG_KEY_DOWN, FG_INPUT_PRESS, now + 100);
    CHECK(fish_game_reel_run(&g) == 2 && fish_game_reel_repeats(&g));
    fish_game_input(&g, FG_KEY_DOWN, FG_INPUT_PRESS, now + 150);
    CHECK(fish_game_reel_run(&g) == 1 && fish_game_reel_repeats(&g));
    fish_game_input(&g, FG_KEY_DOWN, FG_INPUT_PRESS, now + 200);
    CHECK(fish_game_reel_run(&g) == 1 && !fish_game_reel_repeats(&g));
    CHECK(g.step_index == 3);
    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now + 250);
    fish_game_input(&g, FG_KEY_UP, FG_INPUT_PRESS, now + 300);
    CHECK(g.view == FG_VIEW_RESULT && g.outcome == FG_OUTCOME_CAUGHT);
    CHECK(fish_game_reel_run(&g) == 0 && !fish_game_reel_repeats(&g));
}

static void test_reel_failures(void)
{
    fish_game_t g;
    fish_game_init(&g, NULL, 21, 0);
    uint32_t now = cast_until_bite(&g, 0);
    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now);
    g.steps[0] = FG_KEY_UP; // 固定第一步不是 OK，便于检验提竿连按的宽限
    // 提竿后马上又按了一下 OK：宽限期内不算按错。
    CHECK(fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now + 100) == 0);
    CHECK(g.view == FG_VIEW_REEL && g.step_index == 0);
    // 宽限期内按错的不是 OK：照样算按错。
    uint32_t fx = fish_game_input(&g, FG_KEY_DOWN, FG_INPUT_PRESS, now + 150);
    CHECK(fx == (FG_FX_REDRAW | FG_FX_SOUND_ESCAPE | FG_FX_SAVE));
    CHECK(g.outcome == FG_OUTCOME_WRONG_KEY);
    CHECK(fish_game_collected(&g.progress) == 0);

    // 宽限期过后，多按的 OK 就算按错。
    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now + 1000);
    now = cast_until_bite(&g, now + 1500);
    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now);
    g.steps[0] = FG_KEY_UP;
    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now + FG_REEL_GRACE_MS);
    CHECK(g.view == FG_VIEW_RESULT && g.outcome == FG_OUTCOME_WRONG_KEY);

    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now + 1000);
    CHECK(g.view == FG_VIEW_READY);
    now = cast_until_bite(&g, now + 2000);
    fish_game_input(&g, FG_KEY_OK, FG_INPUT_PRESS, now);
    fish_game_input(&g, (fg_key_t)g.steps[0], FG_INPUT_PRESS, now + 1500);
    CHECK(g.step_index == 1);
    // 正确一步后重新计时 2 s。
    CHECK(fish_game_tick(&g, now + 1500 + FG_REEL_STEP_MS - 1) == 0);
    CHECK(fish_game_ms_until_next(&g, now + 1500) == FG_REEL_STEP_MS);
    fish_game_tick(&g, now + 1500 + FG_REEL_STEP_MS);
    CHECK(g.view == FG_VIEW_RESULT && g.outcome == FG_OUTCOME_TOO_SLOW);
    CHECK(g.progress.escapes == 3);
}

static void test_idle_dim_outside_waiting(void)
{
    fish_game_t g;
    fish_game_init(&g, NULL, 7, 0);
    CHECK(fish_game_ms_until_next(&g, 0) == FG_IDLE_DIM_MS);
    fish_game_input(&g, FG_KEY_DOWN, FG_INPUT_PRESS, 30000);
    CHECK(fish_game_tick(&g, 30000 + FG_IDLE_DIM_MS - 1) == 0);
    CHECK(fish_game_tick(&g, 30000 + FG_IDLE_DIM_MS) == FG_FX_SCREEN_OFF);
    // 只剩日计时保存点。
    CHECK(fish_game_ms_until_next(&g, 30000 + FG_IDLE_DIM_MS) == FG_DAY_SAVE_MS - 30000 - FG_IDLE_DIM_MS);
}

static void test_points_gate_cast(void)
{
    fish_game_t g;
    fg_progress_t saved = {.bait = 2, .points = 19};
    fish_game_init(&g, &saved, 7, 0);

    // 亮片 20 积分，19 不够：留在准备页，提示不足，积分和抛竿数不变。
    CHECK(tap(&g, FG_KEY_OK, 100) == FG_FX_UPDATE);
    CHECK(g.view == FG_VIEW_READY && g.short_points);
    CHECK(g.progress.points == 19 && g.progress.casts == 0);

    // 换饵清掉提示；蚯蚓 5 积分可以抛。
    fish_game_input(&g, FG_KEY_DOWN, FG_INPUT_PRESS, 200);
    CHECK(g.progress.bait == 0 && !g.short_points);
    tap(&g, FG_KEY_OK, 300);
    CHECK(g.view == FG_VIEW_WAITING && g.progress.points == 14);
}

static void test_catch_awards_points_except_junk(void)
{
    for (uint8_t e = 0; e < FISH_ENTRY_COUNT; ++e) {
        bool junk_zero = strcmp(FISH_ENTRIES[e].name, "水草") == 0 || strcmp(FISH_ENTRIES[e].name, "旧靴子") == 0;
        CHECK(junk_zero == (FISH_ENTRIES[e].points == 0));
    }

    fish_game_t g;
    fish_game_init(&g, NULL, 7, 0);
    uint32_t bite = cast_until_bite(&g, 0);
    uint32_t before = g.progress.points;
    uint32_t now = bite + 10;
    tap(&g, FG_KEY_OK, now);
    now += FG_REEL_GRACE_MS;
    while (g.view == FG_VIEW_REEL) {
        tap(&g, (fg_key_t)g.steps[g.step_index], ++now);
    }
    CHECK(g.outcome == FG_OUTCOME_CAUGHT);
    CHECK(g.progress.points == before + FISH_ENTRIES[g.entry].points);
}

static void test_daily_grant(void)
{
    fish_game_t g;
    fg_progress_t saved = {.points = 3, .day_ms = FG_DAY_MS - FG_DAY_SAVE_MS - 10};
    fish_game_init(&g, &saved, 7, 1000);
    CHECK(fish_game_ms_until_next(&g, 1000) == 10);
    CHECK(fish_game_tick(&g, 1010) & FG_FX_SAVE); // 小时保存点
    CHECK(g.progress.points == 3);

    uint32_t at = 1010 + FG_DAY_SAVE_MS;
    CHECK(fish_game_tick(&g, 1000 + FG_IDLE_DIM_MS) == FG_FX_SCREEN_OFF); // 黑屏后只剩日计时
    CHECK(fish_game_ms_until_next(&g, at - 1) == 1);
    uint32_t fx = fish_game_tick(&g, at);
    CHECK((fx & (FG_FX_SAVE | FG_FX_UPDATE)) == (FG_FX_SAVE | FG_FX_UPDATE));
    CHECK(g.progress.points == 3 + FG_DAILY_POINTS && g.progress.day_ms == 0);

    // 很久没有 tick（跨两天）：两次都送。
    fish_game_tick(&g, at + 2 * FG_DAY_MS + 5);
    CHECK(g.progress.points == 3 + 3 * FG_DAILY_POINTS && g.progress.day_ms == 5);

    fish_game_t fresh;
    fish_game_init(&fresh, NULL, 7, 0);
    CHECK(fresh.progress.points == FG_DAILY_POINTS);
}

static void test_timer_wraparound(void)
{
    fish_game_t g;
    uint32_t start = UINT32_MAX - 10000U;
    fish_game_init(&g, NULL, 7, start);
    tap(&g, FG_KEY_OK, start);
    CHECK(g.deadline_ms < start); // 已回绕
    CHECK(fish_game_tick(&g, start + 1000) == 0);
    CHECK(g.view == FG_VIEW_WAITING);
    fish_game_tick(&g, g.deadline_ms);
    CHECK(g.view == FG_VIEW_BITE);
}

static void test_save_roundtrip_and_rejects(void)
{
    fg_progress_t p = {.bait = 2, .casts = 70000, .escapes = 12, .points = 123456, .day_ms = 7654321};
    p.counts[0] = 3;
    p.counts[FISH_ENTRY_MAX - 1] = 65535;
    uint8_t buf[FISH_SAVE_SIZE];
    fish_save_encode(&p, buf);

    fg_progress_t q;
    memset(&q, 0xAA, sizeof(q));
    CHECK(fish_save_decode(buf, sizeof(buf), &q));
    CHECK(q.bait == 2 && q.casts == 70000 && q.escapes == 12);
    CHECK(q.points == 123456 && q.day_ms == 7654321);
    CHECK(memcmp(q.counts, p.counts, sizeof(p.counts)) == 0);

    fg_progress_t untouched = q;
    CHECK(!fish_save_decode(buf, sizeof(buf) - 1, &q));
    buf[2] = FISH_SAVE_VERSION + 1;
    CHECK(!fish_save_decode(buf, sizeof(buf), &q));
    buf[2] = FISH_SAVE_VERSION;
    buf[0] = 'X';
    CHECK(!fish_save_decode(buf, sizeof(buf), &q));
    CHECK(!fish_save_decode(NULL, FISH_SAVE_SIZE, &q));
    CHECK(memcmp(&q, &untouched, sizeof(q)) == 0);

    // 版本 1 存档：保留渔获，积分从每日赠送量开始。
    buf[0] = 'F';
    buf[2] = 1;
    CHECK(!fish_save_decode(buf, FISH_SAVE_SIZE, &q));
    CHECK(fish_save_decode(buf, FISH_SAVE_V1_SIZE, &q));
    CHECK(q.casts == 70000 && q.counts[0] == 3);
    CHECK(q.points == FG_DAILY_POINTS && q.day_ms == 0);
}

int main(void)
{
    test_draw_follows_bait_weights();
    test_wait_stays_in_bait_range();
    test_catalog_is_playable();
    test_bait_selection_and_cast();
    test_click_needs_press_in_same_view();
    test_waiting_dims_and_first_press_only_wakes();
    test_bite_wakes_beeps_and_escapes();
    test_reel_success_records_catch();
    test_reel_run_hint();
    test_reel_failures();
    test_idle_dim_outside_waiting();
    test_points_gate_cast();
    test_catch_awards_points_except_junk();
    test_daily_grant();
    test_timer_wraparound();
    test_save_roundtrip_and_rejects();
    puts("test_fish_game: PASS");
    return 0;
}
