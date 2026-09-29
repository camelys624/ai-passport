// main/fish_ui.c — 见 fish_ui.h。
//
// 画面：8-bit 像素风背景（傍晚天空、落日、山丘、湖面，见 fish_sprites.h），
// 水平线在 WATER_Y；天空放标题与渔获像素图，湖面放卡片与操作提示。
// 屏幕四角被 BSP 以 30 px 圆角遮罩，顶部状态行从 x=30 起，其余文字居中。
#include "fish_ui.h"

#include <stdio.h>
#include <string.h>

#include "fish_catalog.h"
#include "fish_fonts.h"
#include "fish_sprites.h"
#include "lvgl.h"

#define SCREEN_W 240
#define SCREEN_H 320
#define WATER_Y 150 // 背景中湖面开始的行（gen_fish_sprites.py 的 HORIZON × SCALE）

#define C_INK 0x2B2118
#define C_INK_SOFT 0x6B5646
#define C_FOAM 0xF4F1E8
#define C_MIST 0xA9CBDC
#define C_ACCENT 0xE8563F
#define C_GOLD 0xF2B632
#define C_ROD 0x5B3A1E
#define C_KEY_UP 0x3FA7D6
#define C_KEY_DOWN 0x7C5CD6

#define BAR_X 40
#define BAR_W 160
#define DOT_SIZE 12
#define DOT_GAP 10
#define PROMPT_SIZE 110
#define PROMPT_PRESSED 88     // 按对一下时从这个尺寸弹回 PROMPT_SIZE
#define PROMPT_CENTER_Y 200
#define POP_MS 160

// 背景动效：云和水波纹从右向左移动，都按整格（FISH_ART_PX）跳动，帧率低，每次只重绘它们
// 所在的小块区域；黑屏时由 fish_ui_pause() 停掉。
#define DRIFT_TICK_MS 100

// 起始 x、y 与每移动一格间隔的节拍数（节拍 = DRIFT_TICK_MS）。
// 云的 y 取状态行、标题、副标题之间的空档（细字压在薄云上会像删除线）；大云近、飘得快一点。
// 水波纹的 y 取各页面湖面上没有文字的行（中间被卡片挡住没关系）；越靠下越近，流得越快。
// 同一行的两道波纹等宽同速，间距始终不变。
static const struct {
    const lv_image_dsc_t *image;
    int16_t x0;
    int16_t y;
    uint8_t ticks;
} DRIFTERS[] = {
    {&fish_img_clouds[0], 9, 30, 9},   // 约 3.3 px/s
    {&fish_img_clouds[1], 183, 75, 12},
    {&fish_img_clouds[2], 111, 3, 16}, // 约 1.9 px/s
    {&fish_img_waves[0], 30, 165, 8},
    {&fish_img_waves[1], 150, 165, 8},
    {&fish_img_waves[2], 90, 228, 7},
    {&fish_img_waves[3], 180, 252, 6},
    {&fish_img_waves[4], 45, 312, 5},
    {&fish_img_waves[5], 165, 312, 5}, // 6 px/s
};
#define DRIFTER_COUNT (sizeof(DRIFTERS) / sizeof(DRIFTERS[0]))
_Static_assert(DRIFTER_COUNT == FISH_CLOUD_COUNT + FISH_WAVE_COUNT, "every cloud and wave needs a DRIFTERS row");

// 位置跨页面保留：换页、亮屏后接着移动，不会跳回起点。
static struct {
    int16_t x[DRIFTER_COUNT];
    uint32_t ticks;
} s_drift;

static struct {
    lv_obj_t *screen;
    lv_obj_t *battery;
    lv_obj_t *elapsed;
    lv_obj_t *bar;
    lv_obj_t *prompt;
    lv_obj_t *prompt_label;
    lv_obj_t *reel_hint;
    lv_obj_t *reel_badge;
    lv_obj_t *reel_badge_label;
    uint8_t shown_step;
    lv_obj_t *dots[FG_REEL_MAX_STEPS];
    lv_obj_t *bait_icon;
    lv_obj_t *bait_index;
    lv_obj_t *bait_name;
    lv_obj_t *bait_hint;
    lv_obj_t *points;
    lv_obj_t *animated[2];
    lv_obj_t *drifters[DRIFTER_COUNT];
    lv_timer_t *drift_timer;
} s_ui;

// ---------------------------------------------------------------------------
// 基本构件
// ---------------------------------------------------------------------------

static lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                     lv_color_t color, lv_opa_t opa, int32_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}

static lv_obj_t *hex_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t rgb, int32_t radius)
{
    return box(parent, x, y, w, h, lv_color_hex(rgb), LV_OPA_COVER, radius);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, int32_t x, int32_t y,
                       int32_t w, lv_text_align_t align, const char *text)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_label_set_text(obj, text);
    return obj;
}

// 整行居中（左右各留 20 px 给圆角）。
static lv_obj_t *line_text(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, int32_t y,
                           const char *text)
{
    return label(parent, font, rgb, 20, y, SCREEN_W - 40, LV_TEXT_ALIGN_CENTER, text);
}

static lv_obj_t *polyline(lv_obj_t *parent, const lv_point_precise_t *points, uint32_t count,
                          lv_color_t color, int32_t width, lv_opa_t opa)
{
    lv_obj_t *obj = lv_line_create(parent);
    lv_line_set_points(obj, points, count); // 保存指针：points 必须是静态数据
    lv_obj_set_style_line_color(obj, color, 0);
    lv_obj_set_style_line_width(obj, width, 0);
    lv_obj_set_style_line_rounded(obj, true, 0);
    lv_obj_set_style_line_opa(obj, opa, 0);
    return obj;
}

static const char *battery_symbol(int battery)
{
    if (battery >= 80) return LV_SYMBOL_BATTERY_FULL;
    if (battery >= 55) return LV_SYMBOL_BATTERY_3;
    if (battery >= 30) return LV_SYMBOL_BATTERY_2;
    if (battery >= 10) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void set_battery(int battery)
{
    if (s_ui.battery == NULL) {
        return;
    }
    if (battery < 0 || battery > 100) {
        lv_label_set_text(s_ui.battery, "");
    } else {
        lv_label_set_text_fmt(s_ui.battery, "%s %d%%", battery_symbol(battery), battery);
    }
}

static void drift_tick(lv_timer_t *timer)
{
    (void)timer;
    ++s_drift.ticks;
    for (size_t i = 0; i < DRIFTER_COUNT; ++i) {
        if (s_drift.ticks % DRIFTERS[i].ticks == 0) {
            s_drift.x[i] -= FISH_ART_PX;
            if (s_drift.x[i] + (int32_t)DRIFTERS[i].image->header.w <= 0) {
                s_drift.x[i] = SCREEN_W; // 从左边出去，再从右边进来
            }
            lv_obj_set_x(s_ui.drifters[i], s_drift.x[i]);
        }
    }
}

// 像素背景 + 移动的云与水波纹 + 顶部状态行。动效层在背景之上、页面内容之下。
static lv_obj_t *scene(const char *status, int battery)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_t *bg;

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0), 0); // 背景图不透明、铺满全屏；LVGL 会从它开始绘制
    bg = lv_image_create(scr);
    lv_image_set_src(bg, &fish_img_background);
    lv_obj_set_pos(bg, 0, 0);
    for (size_t i = 0; i < DRIFTER_COUNT; ++i) {
        s_ui.drifters[i] = lv_image_create(scr);
        lv_image_set_src(s_ui.drifters[i], DRIFTERS[i].image);
        lv_obj_set_pos(s_ui.drifters[i], s_drift.x[i], DRIFTERS[i].y);
    }
    s_ui.drift_timer = lv_timer_create(drift_tick, DRIFT_TICK_MS, NULL);

    label(scr, fish_font_body, C_INK, 30, 12, 110, LV_TEXT_ALIGN_LEFT, status);
    s_ui.battery = label(scr, fish_font_body, C_INK, 130, 12, 80, LV_TEXT_ALIGN_RIGHT, "");
    set_battery(battery);
    return scr;
}

static lv_obj_t *bar(lv_obj_t *parent, int32_t y, int32_t h)
{
    box(parent, BAR_X, y, BAR_W, h, lv_color_hex(C_FOAM), LV_OPA_30, h / 2);
    return box(parent, BAR_X, y, BAR_W, h, lv_color_hex(C_GOLD), LV_OPA_COVER, h / 2);
}

static void set_bar(uint32_t remaining_ms, uint32_t total_ms)
{
    int32_t w;

    if (s_ui.bar == NULL) {
        return;
    }
    if (remaining_ms > total_ms) {
        remaining_ms = total_ms;
    }
    w = (int32_t)((uint64_t)BAR_W * remaining_ms / total_ms);
    if (w < 1) {
        lv_obj_add_flag(s_ui.bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_ui.bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_ui.bar, w);
    }
}

static uint32_t remaining(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(deadline_ms - now_ms) > 0 ? deadline_ms - now_ms : 0U;
}

// ---------------------------------------------------------------------------
// 动画
// ---------------------------------------------------------------------------

static void anim_y(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void anim_ripple(void *var, int32_t v)
{
    lv_obj_t *ring = var;
    lv_obj_set_size(ring, v, v / 3);
    lv_obj_set_style_border_opa(ring, (lv_opa_t)(255 - v * 255 / 90), 0);
}

static void start_anim(lv_obj_t *obj, lv_anim_exec_xcb_t exec, int32_t from, int32_t to,
                       uint32_t duration, uint32_t delay, bool reverse)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_delay(&a, delay);
    if (reverse) {
        lv_anim_set_reverse_duration(&a, duration);
    }
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
    for (size_t i = 0; i < sizeof(s_ui.animated) / sizeof(s_ui.animated[0]); ++i) {
        if (s_ui.animated[i] == NULL) {
            s_ui.animated[i] = obj;
            break;
        }
    }
}

void fish_ui_pause(void)
{
    for (size_t i = 0; i < sizeof(s_ui.animated) / sizeof(s_ui.animated[0]); ++i) {
        if (s_ui.animated[i] != NULL) {
            lv_anim_delete(s_ui.animated[i], NULL);
            s_ui.animated[i] = NULL;
        }
    }
    if (s_ui.drift_timer != NULL) {
        lv_timer_delete(s_ui.drift_timer);
        s_ui.drift_timer = NULL;
    }
}

// ---------------------------------------------------------------------------
// 钓竿、浮标与渔获造型
// ---------------------------------------------------------------------------

static const lv_point_precise_t ROD_IDLE[] = {{250, 330}, {152, 70}};
static const lv_point_precise_t LINE_IDLE[] = {{152, 70}, {76, 146}};
static const lv_point_precise_t ROD_BENT[] = {{250, 330}, {176, 134}, {138, 96}};
static const lv_point_precise_t LINE_TAUT[] = {{138, 96}, {76, 160}};

static lv_obj_t *float_bob(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *holder = box(parent, x, y, 12, 22, lv_color_hex(0), LV_OPA_TRANSP, 0);
    hex_box(holder, 2, 10, 8, 12, C_FOAM, 3);
    hex_box(holder, 0, 0, 12, 12, C_ACCENT, LV_RADIUS_CIRCLE);
    return holder;
}

static lv_obj_t *ring(lv_obj_t *parent, int32_t w, lv_opa_t opa)
{
    lv_obj_t *obj = box(parent, 0, 0, w, w / 3, lv_color_hex(0), LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(obj, lv_color_hex(C_FOAM), 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_opa(obj, opa, 0);
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
    return obj;
}

// 以 (cx, cy) 为中心放渔获像素图；未发现的用同形状的剪影。
static void specimen(lv_obj_t *parent, int32_t cx, int32_t cy, uint8_t entry, bool known)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, known ? &fish_img_entries[entry] : &fish_img_shadows[entry]);
    lv_obj_set_pos(img, cx - FISH_SPRITE_W / 2, cy - FISH_SPRITE_H / 2);
}

static void rarity_chip(lv_obj_t *parent, const fish_entry_t *entry, bool known)
{
    static const char *const NAMES[] = {"普通", "少见", "稀有"};
    static const uint32_t COLORS[] = {0x8C9A94, C_KEY_UP, C_GOLD};
    uint8_t rarity = entry->rarity <= FISH_RARITY_RARE ? entry->rarity : FISH_RARITY_COMMON;
    lv_obj_t *chip = hex_box(parent, 84, 40, 72, 24, known ? COLORS[rarity] : 0x7A6A5A, 12);
    lv_obj_t *text = label(chip, fish_font_body, known && rarity == FISH_RARITY_RARE ? C_INK : C_FOAM,
                           0, 0, 72, LV_TEXT_ALIGN_CENTER, known ? NAMES[rarity] : "未发现");
    lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);
}

static lv_obj_t *card(lv_obj_t *parent)
{
    return hex_box(parent, 20, 162, 200, 100, C_FOAM, 14);
}

static void card_texts(lv_obj_t *c, const char *title, const char *body)
{
    label(c, fish_font_title, C_INK, 0, 8, 200, LV_TEXT_ALIGN_CENTER, title);
    label(c, fish_font_body, C_INK_SOFT, 12, 46, 176, LV_TEXT_ALIGN_CENTER, body);
}

// ---------------------------------------------------------------------------
// 页面
// ---------------------------------------------------------------------------

static lv_obj_t *render_ready(const fish_game_t *game, int battery)
{
    char status[24];
    lv_obj_t *scr;
    lv_obj_t *c;

    snprintf(status, sizeof(status), "鱼册 %u/%u", (unsigned)fish_game_collected(&game->progress),
             (unsigned)FISH_ENTRY_COUNT);
    scr = scene(status, battery);
    line_text(scr, fish_font_title, C_INK, 50, "口袋垂钓");
    s_ui.points = line_text(scr, fish_font_body, C_INK_SOFT, 88, "");

    // 饵料卡：左边像素图标，右边名称与每竿积分，底部一句提示（积分不足时改为提醒）。
    c = card(scr);
    s_ui.bait_icon = lv_image_create(c);
    lv_obj_set_pos(s_ui.bait_icon, 12, 8);
    s_ui.bait_name = label(c, fish_font_title, C_INK, 72, 8, 116, LV_TEXT_ALIGN_LEFT, "");
    s_ui.bait_index = label(c, fish_font_body, C_INK_SOFT, 72, 40, 84, LV_TEXT_ALIGN_LEFT, "");
    label(c, fish_font_body, C_ACCENT, 156, 40, 32, LV_TEXT_ALIGN_RIGHT, "▲▼");
    s_ui.bait_hint = label(c, fish_font_body, C_INK_SOFT, 0, 68, 200, LV_TEXT_ALIGN_CENTER, "");

    line_text(scr, fish_font_body, C_FOAM, 272, "OK 抛竿　▲▼ 换饵");
    line_text(scr, fish_font_body, C_MIST, 294, "长按 OK 打开鱼册");
    return scr;
}

static lv_obj_t *render_waiting(const fish_game_t *game, int battery)
{
    char text[32];
    lv_obj_t *scr = scene("等待中", battery);
    lv_obj_t *bob;

    lv_obj_align(ring(scr, 34, LV_OPA_50), LV_ALIGN_TOP_LEFT, 59, WATER_Y - 3);
    polyline(scr, ROD_IDLE, 2, lv_color_hex(C_ROD), 5, LV_OPA_COVER);
    polyline(scr, LINE_IDLE, 2, lv_color_hex(C_FOAM), 1, LV_OPA_70);
    bob = float_bob(scr, 70, WATER_Y - 10);
    start_anim(bob, anim_y, WATER_Y - 13, WATER_Y - 7, 1300, 0, true);

    line_text(scr, fish_font_title, C_FOAM, 176, "等鱼上钩…");
    snprintf(text, sizeof(text), "饵料：%s", FISH_BAITS[game->progress.bait].name);
    line_text(scr, fish_font_body, C_MIST, 212, text);
    s_ui.elapsed = line_text(scr, fish_font_body, C_MIST, 234, "");
    line_text(scr, fish_font_body, C_FOAM, 262, "去忙吧，咬钩会滴一声");
    line_text(scr, fish_font_body, C_MIST, 290, "长按 OK 收竿");
    return scr;
}

static lv_obj_t *render_bite(int battery)
{
    lv_obj_t *scr = scene("咬钩了", battery);
    lv_obj_t *splash = box(scr, 36, WATER_Y - 23, 80, 50, lv_color_hex(0), LV_OPA_TRANSP, 0);

    start_anim(ring(splash, 10, LV_OPA_COVER), anim_ripple, 10, 80, 1000, 0, false);
    start_anim(ring(splash, 10, LV_OPA_COVER), anim_ripple, 10, 80, 1000, 500, false);
    polyline(scr, ROD_BENT, 3, lv_color_hex(C_ROD), 5, LV_OPA_COVER);
    polyline(scr, LINE_TAUT, 2, lv_color_hex(C_FOAM), 1, LV_OPA_90);

    line_text(scr, fish_font_large, C_ACCENT, 42, "咬钩了！");
    line_text(scr, fish_font_title, C_FOAM, 186, "按 OK 提竿");
    s_ui.bar = bar(scr, 232, 10);
    line_text(scr, fish_font_body, C_MIST, 258, "晚了鱼就跑了");
    return scr;
}

static lv_obj_t *render_reel(const fish_game_t *game, int battery)
{
    lv_obj_t *scr = scene("收线", battery);
    int32_t total = game->step_count * DOT_SIZE + (game->step_count - 1) * DOT_GAP;
    int32_t x = (SCREEN_W - total) / 2;

    line_text(scr, fish_font_title, C_INK, 42, "收线！");
    s_ui.reel_hint = line_text(scr, fish_font_body, C_INK_SOFT, 80, "");

    // 居中对齐：按下时的缩放动画以圆心为中心。
    s_ui.prompt = hex_box(scr, 0, 0, PROMPT_SIZE, PROMPT_SIZE, C_ACCENT, LV_RADIUS_CIRCLE);
    lv_obj_align(s_ui.prompt, LV_ALIGN_CENTER, 0, PROMPT_CENTER_Y - SCREEN_H / 2);
    lv_obj_set_style_border_color(s_ui.prompt, lv_color_hex(C_FOAM), 0);
    lv_obj_set_style_border_width(s_ui.prompt, 4, 0);
    s_ui.prompt_label = label(s_ui.prompt, fish_font_large, C_FOAM, 0, 0, PROMPT_SIZE,
                              LV_TEXT_ALIGN_CENTER, "");
    lv_obj_align(s_ui.prompt_label, LV_ALIGN_CENTER, 0, 0);

    // 连按角标：同一个键还要连按几下。
    s_ui.reel_badge = hex_box(scr, 150, PROMPT_CENTER_Y - 60, 42, 42, C_GOLD, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(s_ui.reel_badge, lv_color_hex(C_FOAM), 0);
    lv_obj_set_style_border_width(s_ui.reel_badge, 3, 0);
    s_ui.reel_badge_label = label(s_ui.reel_badge, fish_font_title, C_INK, 0, 0, 42,
                                  LV_TEXT_ALIGN_CENTER, "");
    lv_obj_align(s_ui.reel_badge_label, LV_ALIGN_CENTER, 0, 0);

    for (uint8_t i = 0; i < game->step_count; ++i) {
        s_ui.dots[i] = hex_box(scr, x + i * (DOT_SIZE + DOT_GAP), 266, DOT_SIZE, DOT_SIZE, C_FOAM,
                               LV_RADIUS_CIRCLE);
    }
    s_ui.bar = bar(scr, 290, 8);
    s_ui.shown_step = game->step_index;
    return scr;
}

static lv_obj_t *render_result(const fish_game_t *game, int battery)
{
    const fish_entry_t *entry = &FISH_ENTRIES[game->entry];
    char text[48];
    lv_obj_t *scr;

    if (game->outcome == FG_OUTCOME_CAUGHT) {
        scr = scene("钓到了", battery);
        rarity_chip(scr, entry, true);
        specimen(scr, 120, 104, game->entry, true);
        card_texts(card(scr), entry->name, entry->desc);
        snprintf(text, sizeof(text), "第 %u 次收获", (unsigned)game->progress.counts[game->entry]);
        line_text(scr, fish_font_body, C_FOAM, 272, text);
        line_text(scr, fish_font_body, C_MIST, 294, "OK 继续");
        return scr;
    }

    scr = scene("跑掉了", battery);
    line_text(scr, fish_font_title, C_INK, 40, "鱼跑了…");
    specimen(scr, 120, 112, game->entry, false);

    switch (game->outcome) {
    case FG_OUTCOME_MISSED:
        snprintf(text, sizeof(text), "咬钩后 %u 秒内按 OK", (unsigned)(FG_BITE_WINDOW_MS / 1000U));
        card_texts(card(scr), "没来得及提竿", text);
        break;
    case FG_OUTCOME_WRONG_KEY:
        card_texts(card(scr), "按错了键", "照着屏幕上的提示按");
        break;
    default:
        snprintf(text, sizeof(text), "每一步要在 %u 秒内按下", (unsigned)(FG_REEL_STEP_MS / 1000U));
        card_texts(card(scr), "收线太慢了", text);
        break;
    }
    snprintf(text, sizeof(text), "差点钓到：%s", entry->name);
    line_text(scr, fish_font_body, C_FOAM, 272, text);
    line_text(scr, fish_font_body, C_MIST, 294, "OK 继续");
    return scr;
}

static lv_obj_t *render_album(const fish_game_t *game, int battery)
{
    uint8_t index = game->album_index;
    const fish_entry_t *entry = &FISH_ENTRIES[index];
    uint16_t count = game->progress.counts[index];
    uint16_t stock = game->progress.stock[index];
    bool known = count > 0;
    char text[48];
    char body[64];
    lv_obj_t *scr;

    snprintf(text, sizeof(text), "积分 %lu", (unsigned long)game->progress.points);
    scr = scene(text, battery);
    rarity_chip(scr, entry, known);
    specimen(scr, 120, 104, index, known);

    if (game->sell_qty > 0) {
        snprintf(text, sizeof(text), "卖出%s", entry->name);
        snprintf(body, sizeof(body), "%u 条 = %lu 积分\n鱼篓共 %u 条", (unsigned)game->sell_qty,
                 (unsigned long)game->sell_qty * entry->price, (unsigned)stock);
        card_texts(card(scr), text, body);
        line_text(scr, fish_font_body, C_FOAM, 272, "▲▼ 选数量");
        line_text(scr, fish_font_body, C_MIST, 294, "OK 卖出　长按 取消");
        return scr;
    }

    card_texts(card(scr), known ? entry->name : "？？？", known ? entry->desc : "还没有钓到过");
    if (!known) {
        snprintf(text, sizeof(text), "%u/%u · 未发现", (unsigned)index + 1U, (unsigned)FISH_ENTRY_COUNT);
    } else if (entry->price == 0) {
        snprintf(text, sizeof(text), "%u/%u · 收获 %u · 不能卖", (unsigned)index + 1U,
                 (unsigned)FISH_ENTRY_COUNT, (unsigned)count);
    } else {
        snprintf(text, sizeof(text), "%u/%u · 鱼篓 %u · 单价 %u", (unsigned)index + 1U,
                 (unsigned)FISH_ENTRY_COUNT, (unsigned)stock, (unsigned)entry->price);
    }
    line_text(scr, fish_font_body, C_FOAM, 272, text);
    line_text(scr, fish_font_body, C_MIST, 294,
              stock > 0 && entry->price > 0 ? "OK 卖鱼　长按 返回" : "▲▼ 翻页　长按 返回");
    return scr;
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

void fish_ui_init(void)
{
    fish_fonts_init();
    memset(&s_ui, 0, sizeof(s_ui));
    memset(&s_drift, 0, sizeof(s_drift));
    for (size_t i = 0; i < DRIFTER_COUNT; ++i) {
        s_drift.x[i] = DRIFTERS[i].x0;
    }
}

void fish_ui_render(const fish_game_t *game, uint32_t now_ms, int battery)
{
    lv_obj_t *old = s_ui.screen;
    lv_obj_t *scr;

    fish_ui_pause();
    memset(&s_ui, 0, sizeof(s_ui));
    switch (game->view) {
    case FG_VIEW_WAITING:
        scr = render_waiting(game, battery);
        break;
    case FG_VIEW_BITE:
        scr = render_bite(battery);
        break;
    case FG_VIEW_REEL:
        scr = render_reel(game, battery);
        break;
    case FG_VIEW_RESULT:
        scr = render_result(game, battery);
        break;
    case FG_VIEW_ALBUM:
        scr = render_album(game, battery);
        break;
    default:
        scr = render_ready(game, battery);
        break;
    }
    s_ui.screen = scr;
    fish_ui_refresh(game, now_ms, battery);
    lv_screen_load(scr);
    if (old != NULL) {
        lv_obj_delete(old);
    }
}

static void refresh_ready(const fish_game_t *game)
{
    const fish_bait_t *bait = &FISH_BAITS[game->progress.bait];

    if (s_ui.bait_name == NULL) {
        return;
    }
    lv_label_set_text_fmt(s_ui.points, "我的积分：%lu", (unsigned long)game->progress.points);
    lv_label_set_text_fmt(s_ui.bait_index, "%u 积分/竿", (unsigned)bait->cost);
    lv_image_set_src(s_ui.bait_icon, &fish_img_baits[game->progress.bait]);
    lv_label_set_text(s_ui.bait_name, bait->name);
    if (game->short_points) {
        lv_label_set_text_fmt(s_ui.bait_hint, "积分不足，每天送 %u", (unsigned)FG_DAILY_POINTS);
        lv_obj_set_style_text_color(s_ui.bait_hint, lv_color_hex(C_ACCENT), 0);
    } else {
        lv_label_set_text(s_ui.bait_hint, bait->hint);
        lv_obj_set_style_text_color(s_ui.bait_hint, lv_color_hex(C_INK_SOFT), 0);
    }
}

static void anim_size(void *var, int32_t v)
{
    lv_obj_set_size((lv_obj_t *)var, v, v);
}

// 按对一下：圆形提示先缩小再弹回，同一个键连按时也能看到“按到了”。
static void pop_prompt(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ui.prompt);
    lv_anim_set_exec_cb(&a, anim_size);
    lv_anim_set_values(&a, PROMPT_PRESSED, PROMPT_SIZE);
    lv_anim_set_duration(&a, POP_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
}

static void refresh_reel(const fish_game_t *game, uint32_t now_ms)
{
    static const char *const KEYS[] = {"▲", "▼", "OK"};
    static const uint32_t COLORS[] = {C_KEY_UP, C_KEY_DOWN, C_ACCENT};
    uint8_t key = game->steps[game->step_index < game->step_count ? game->step_index : 0];
    uint8_t run = fish_game_reel_run(game);
    bool repeats = fish_game_reel_repeats(game);

    if (s_ui.prompt == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(s_ui.prompt, lv_color_hex(COLORS[key]), 0);
    lv_label_set_text(s_ui.prompt_label, KEYS[key]);
    if (game->step_index != s_ui.shown_step) {
        s_ui.shown_step = game->step_index;
        pop_prompt();
    }

    // 连按提示：第一下说“连按 N 下”，之后说“再按 N 下”，角标显示剩余次数。
    if (repeats) {
        lv_label_set_text_fmt(s_ui.reel_hint, "再按 %u 下", (unsigned)run);
    } else if (run >= 2) {
        lv_label_set_text_fmt(s_ui.reel_hint, "连按 %u 下", (unsigned)run);
    } else {
        lv_label_set_text(s_ui.reel_hint, "照着提示按键");
    }
    lv_obj_set_style_text_color(s_ui.reel_hint, lv_color_hex(repeats || run >= 2 ? C_ACCENT : C_INK_SOFT), 0);
    lv_obj_set_style_text_font(s_ui.reel_hint, repeats || run >= 2 ? fish_font_title : fish_font_body, 0);
    if (run >= 2) {
        lv_label_set_text_fmt(s_ui.reel_badge_label, "×%u", (unsigned)run);
        lv_obj_clear_flag(s_ui.reel_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.reel_badge, LV_OBJ_FLAG_HIDDEN);
    }

    for (uint8_t i = 0; i < game->step_count; ++i) {
        if (s_ui.dots[i] == NULL) {
            continue;
        }
        lv_obj_set_style_bg_color(s_ui.dots[i], lv_color_hex(i < game->step_index ? C_GOLD : C_FOAM), 0);
        lv_obj_set_style_bg_opa(s_ui.dots[i], i <= game->step_index ? LV_OPA_COVER : LV_OPA_30, 0);
    }
    set_bar(remaining(now_ms, game->deadline_ms), FG_REEL_STEP_MS);
}

void fish_ui_refresh(const fish_game_t *game, uint32_t now_ms, int battery)
{
    set_battery(battery);
    switch (game->view) {
    case FG_VIEW_READY:
        refresh_ready(game);
        break;
    case FG_VIEW_WAITING:
        if (s_ui.elapsed != NULL) {
            uint32_t seconds = (now_ms - game->cast_ms) / 1000U;
            lv_label_set_text_fmt(s_ui.elapsed, "已等 %u:%02u", (unsigned)(seconds / 60U),
                                  (unsigned)(seconds % 60U));
        }
        break;
    case FG_VIEW_BITE:
        set_bar(remaining(now_ms, game->deadline_ms), FG_BITE_WINDOW_MS);
        break;
    case FG_VIEW_REEL:
        refresh_reel(game, now_ms);
        break;
    default:
        break;
    }
}
