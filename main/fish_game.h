// main/fish_game.h — 钓鱼玩法状态机（纯 C，不依赖 ESP-IDF/LVGL，可在主机上测试）。
//
// 流程：准备（选饵）→ 等待（可黑屏）→ 咬钩（滴声 + 亮屏，限时提竿）→ 收线（按提示逐键）
//       → 结果（钓到 / 跑掉）→ 准备。准备页长按 OK 进入鱼册。
//
// 输入约定（与 bsp_btn_ev_t 对应）：
//   PRESS  按下瞬间，用于换饵、提竿、收线、结果页继续、鱼册翻页与返回等需要即时响应、
//          可以连按的操作。按键组件在连按 3 次以上时不报 CLICK/DOUBLE，所以“继续/返回”
//          不依赖 CLICK。
//   CLICK  抬起后确认的单击（双击也按单击处理），只用于准备页抛竿——同一个 OK 长按要进鱼册。
//   LONG   长按，用于进入鱼册、等待中收竿。
// CLICK/LONG 只有在“当前页面收到过同一键的 PRESS”时才生效：上一页最后一次按键的
// 抬起事件不会穿透到新页面。结果页出现后需要先停手 FG_RESULT_GUARD_MS（期间每次按键
// 都重新计时），再按 OK 才继续，连按收线时不会把结果页直接跳过。
//
// 黑屏：等待中 5 s 无操作、其他页 60 s 无操作时请求关屏；咬钩和收线期间不关屏。
// 黑屏时的第一次按下只用于亮屏，不触发任何玩法动作。
//
// 所有时间参数为单调毫秒计数，允许 uint32_t 回绕。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fish_catalog.h"

#define FG_BITE_WINDOW_MS 6000U
#define FG_BITE_BEEP_INTERVAL_MS 2000U
#define FG_REEL_STEP_MS 2000U
#define FG_WAIT_DIM_MS 5000U
#define FG_IDLE_DIM_MS 60000U
#define FG_REEL_MAX_STEPS 8U
#define FG_RESULT_GUARD_MS 600U
#define FG_REEL_GRACE_MS 400U  // 收线开始后这段时间内多按的 OK 不算按错（提竿常连按两下）
#define FG_NO_DEADLINE UINT32_MAX

typedef enum {
    FG_VIEW_READY = 0,
    FG_VIEW_WAITING,
    FG_VIEW_BITE,
    FG_VIEW_REEL,
    FG_VIEW_RESULT,
    FG_VIEW_ALBUM,
} fg_view_t;

// 与 bsp_btn_t 的数值一致。
typedef enum {
    FG_KEY_UP = 0,
    FG_KEY_DOWN,
    FG_KEY_OK,
} fg_key_t;

typedef enum {
    FG_INPUT_PRESS = 0,
    FG_INPUT_CLICK,
    FG_INPUT_LONG,
} fg_input_t;

typedef enum {
    FG_OUTCOME_CAUGHT = 0,
    FG_OUTCOME_MISSED,     // 咬钩后没有及时提竿
    FG_OUTCOME_WRONG_KEY,  // 收线时按错键
    FG_OUTCOME_TOO_SLOW,   // 收线某一步超时
} fg_outcome_t;

// fish_game_input()/fish_game_tick() 返回的副作用请求（位掩码），由应用层执行。
enum {
    FG_FX_REDRAW = 1U << 0,        // 页面切换，需要重建界面
    FG_FX_UPDATE = 1U << 1,        // 同一页面内容变化（换饵、收线进度、鱼册选中项）
    FG_FX_SCREEN_ON = 1U << 2,
    FG_FX_SCREEN_OFF = 1U << 3,
    FG_FX_SOUND_BITE = 1U << 4,    // 咬钩提示“滴滴”
    FG_FX_SOUND_CATCH = 1U << 5,
    FG_FX_SOUND_ESCAPE = 1U << 6,
    FG_FX_SAVE = 1U << 7,          // 持久化 progress
};

// 需要跨断电保存的进度。
typedef struct {
    uint8_t bait;                       // 上次抛竿用的饵料
    uint32_t casts;                     // 累计抛竿次数
    uint32_t escapes;                   // 累计跑鱼次数
    uint16_t counts[FISH_ENTRY_MAX];    // 每种渔获的累计数量，下标对应 FISH_ENTRIES
} fg_progress_t;

typedef struct {
    fg_view_t view;
    bool screen_on;
    uint32_t rng;
    fg_progress_t progress;
    uint32_t last_activity_ms;
    uint32_t cast_ms;          // 本竿抛出的时刻
    uint32_t deadline_ms;      // 等待：咬钩时刻；咬钩：提竿截止；收线：本步截止
    uint32_t next_beep_ms;     // 咬钩期间下一次提示音
    uint8_t entry;             // 本竿咬钩的渔获（咬钩时抽取）
    uint8_t steps[FG_REEL_MAX_STEPS];
    uint8_t step_count;
    uint8_t step_index;
    fg_outcome_t outcome;
    uint32_t result_ms;        // 结果页安静期起点：进入时刻，或安静期内最后一次按键
    uint8_t album_index;
    uint8_t armed;             // 当前页面收到过 PRESS 的键（位掩码）
} fish_game_t;

// saved 可为 NULL（全新存档）；非法饵料下标回退到 0。seed 为 0 时使用内置非零种子。
void fish_game_init(fish_game_t *game, const fg_progress_t *saved, uint32_t seed, uint32_t now_ms);

uint32_t fish_game_input(fish_game_t *game, fg_key_t key, fg_input_t input, uint32_t now_ms);

// 推进计时：咬钩、提示音、超时跑鱼、自动黑屏。可以任意频率调用。
uint32_t fish_game_tick(fish_game_t *game, uint32_t now_ms);

// 距离下一个计时事件还有多少毫秒；没有待定事件时返回 FG_NO_DEADLINE。
uint32_t fish_game_ms_until_next(const fish_game_t *game, uint32_t now_ms);

// 按饵料权重抽取渔获下标。
uint8_t fish_game_draw(uint32_t *rng, uint8_t bait);

// 在饵料的等待区间内抽取咬钩等待时长。
uint32_t fish_game_wait_ms(uint32_t *rng, uint8_t bait);

// 已收集（数量 > 0）的渔获种类数。
uint8_t fish_game_collected(const fg_progress_t *progress);

// 收线页：从当前步起连续相同按键还剩几下（含当前这一下）；不在收线页时返回 0。
uint8_t fish_game_reel_run(const fish_game_t *game);

// 收线页：当前这一下是否与上一下同键（连按中的后续一下）。
bool fish_game_reel_repeats(const fish_game_t *game);
