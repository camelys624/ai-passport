// main/codeloom_ui.c — 见 codeloom_ui.h。
//
// 设计：深色背景（省电、夜间不刺眼）、圆角卡片、强调色区分审批类型。屏幕四角有 30 px
// 圆角遮罩，所有内容都留出至少 12 px 边距。页面切换或列表内容变化时重建内容区；
// 选择、年龄、发送状态等只做原位更新，减少 LVGL 堆碎片。
#include "codeloom_ui.h"

#include <stdio.h>
#include <string.h>

#include "bsp_pins.h"
#include "codeloom_fonts.h"
#include "codeloom_text.h"
#include "esp_log.h"
#include "lvgl.h"

#define COL_BG 0x0E1116
#define COL_SURFACE 0x181C24
#define COL_SURFACE_HI 0x232A36
#define COL_BORDER 0x2C3340
#define COL_TEXT 0xE9ECF2
#define COL_MUTED 0x8C94A3
#define COL_ACCENT 0x6C8CFF
#define COL_AMBER 0xF2A93B
#define COL_GREEN 0x37C47A
#define COL_RED 0xEF5B5B
#define COL_CYAN 0x3FB6D8
#define COL_GRAY 0x5A6272

#define SIDE 12
#define CARD_W (BSP_LCD_W - 2 * SIDE)
#define HEADER_H 52
#define BANNER_H 26
#define FOOTER_Y 290
#define SCREEN_BOTTOM 316
#define BUTTON_H 30
#define BUTTON_GAP 4

typedef enum {
    MODE_NONE = 0,
    MODE_MAIN,
    MODE_SETUP,
} ui_mode_t;

typedef struct {
    ui_mode_t mode;
    lv_obj_t *screen;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *bars[CL_PAGE_COUNT];
    lv_obj_t *battery;
    lv_obj_t *banner;
    lv_obj_t *banner_label;
    lv_obj_t *body;
    lv_obj_t *footer;
    lv_obj_t *toast;
    lv_obj_t *toast_label;

    // 当前内容区的对象；重建内容区时全部失效。
    lv_obj_t *list;
    lv_obj_t *cards[CL_MAX_TASKS];
    lv_obj_t *card_meta[CL_MAX_TASKS];
    uint8_t card_count;
    lv_obj_t *status_values[CL_STATUS_ROWS];
    lv_obj_t *detail_chip;
    lv_obj_t *detail_age;
    lv_obj_t *detail_box;
    lv_obj_t *detail_title;
    lv_obj_t *detail_text;
    lv_obj_t *detail_line;
    lv_obj_t *detail_page;
    lv_obj_t *buttons[CL_DECISION_COUNT];
    lv_obj_t *button_labels[CL_DECISION_COUNT];
    lv_obj_t *confirm_buttons[2];

    // 已渲染内容的标识。
    bool built;
    cl_page_t page;
    cl_view_t view;
    uint32_t gen;
    uint8_t visible;
    bool has_overview;
    char detail_id[CL_ID_MAX];
    int body_top;
    int body_h;
} ui_t;

static ui_t ui;
static char s_scratch[CL_APPROVAL_DETAIL_MAX + 128];

static lv_style_t st_box;
static lv_style_t st_card;
static lv_style_t st_card_selected;
static lv_style_t st_chip;
static lv_style_t st_button;

// ---------------------------------------------------------------------------
// 基础构件

static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &st_box, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

// 设置文本：先按标签字体做字形替换，保证不支持的字符显示为 “□”。
static void set_text(lv_obj_t *label, const char *text)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    codeloom_fonts_sanitize(font, text != NULL ? text : "", s_scratch, sizeof(s_scratch));
    lv_label_set_text(label, s_scratch);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    set_text(obj, text);
    return obj;
}

static lv_obj_t *chip(lv_obj_t *parent, const char *text, uint32_t bg, uint32_t fg, bool outline)
{
    lv_obj_t *obj = label(parent, cl_font_small, fg, text);
    lv_obj_add_style(obj, &st_chip, 0);
    if (outline) {
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(bg), 0);
    } else {
        lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    }
    return obj;
}

static int32_t line_height(const lv_font_t *font)
{
    return lv_font_get_line_height(font);
}

static void init_styles(void)
{
    lv_style_init(&st_box);
    lv_style_set_bg_opa(&st_box, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_box, 0);
    lv_style_set_pad_all(&st_box, 0);

    lv_style_init(&st_card);
    lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
    lv_style_set_bg_color(&st_card, lv_color_hex(COL_SURFACE));
    lv_style_set_radius(&st_card, 12);
    lv_style_set_border_width(&st_card, 2);
    lv_style_set_border_color(&st_card, lv_color_hex(COL_SURFACE));

    lv_style_init(&st_card_selected);
    lv_style_set_bg_color(&st_card_selected, lv_color_hex(COL_SURFACE_HI));
    lv_style_set_border_color(&st_card_selected, lv_color_hex(COL_ACCENT));

    lv_style_init(&st_chip);
    lv_style_set_bg_opa(&st_chip, LV_OPA_COVER);
    lv_style_set_radius(&st_chip, 6);
    lv_style_set_pad_hor(&st_chip, 6);
    lv_style_set_pad_ver(&st_chip, 1);

    lv_style_init(&st_button);
    lv_style_set_bg_opa(&st_button, LV_OPA_COVER);
    lv_style_set_bg_color(&st_button, lv_color_hex(COL_SURFACE));
    lv_style_set_radius(&st_button, 10);
    lv_style_set_border_width(&st_button, 1);
}

// ---------------------------------------------------------------------------
// 固定外框：标题、分页指示、电量、横幅、内容区、底部提示、toast

void codeloom_ui_init(void)
{
    init_styles();
    ui.screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui.screen);
    lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui.screen, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    ui.title = label(ui.screen, cl_font_title, COL_TEXT, "Codeloom");
    lv_obj_set_pos(ui.title, 20, 10);
    ui.subtitle = label(ui.screen, cl_font_small, COL_MUTED, "");

    for (int i = 0; i < CL_PAGE_COUNT; ++i) {
        ui.bars[i] = box(ui.screen);
        lv_obj_set_size(ui.bars[i], 18, 3);
        lv_obj_set_pos(ui.bars[i], 20 + i * 24, 44);
        lv_obj_set_style_radius(ui.bars[i], 2, 0);
        lv_obj_set_style_bg_opa(ui.bars[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(ui.bars[i], LV_OBJ_FLAG_HIDDEN);
    }

    ui.battery = label(ui.screen, cl_font_small, COL_MUTED, "");
    lv_obj_align(ui.battery, LV_ALIGN_TOP_RIGHT, -20, 16);
    lv_obj_add_flag(ui.battery, LV_OBJ_FLAG_HIDDEN);

    ui.banner = box(ui.screen);
    lv_obj_set_size(ui.banner, CARD_W, BANNER_H);
    lv_obj_set_pos(ui.banner, SIDE, HEADER_H);
    lv_obj_set_style_radius(ui.banner, 8, 0);
    lv_obj_set_style_bg_opa(ui.banner, LV_OPA_COVER, 0);
    ui.banner_label = label(ui.banner, cl_font_small, COL_BG, "");
    lv_obj_center(ui.banner_label);
    lv_obj_add_flag(ui.banner, LV_OBJ_FLAG_HIDDEN);

    ui.body = box(ui.screen);
    ui.footer = label(ui.screen, cl_font_small, COL_MUTED, "");
    lv_obj_set_width(ui.footer, 216);
    lv_obj_set_style_text_align(ui.footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui.footer, SIDE, FOOTER_Y);

    ui.toast = box(ui.screen);
    lv_obj_set_size(ui.toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ui.toast, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui.toast, lv_color_hex(COL_SURFACE_HI), 0);
    lv_obj_set_style_border_width(ui.toast, 1, 0);
    lv_obj_set_style_border_color(ui.toast, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_radius(ui.toast, 14, 0);
    lv_obj_set_style_pad_hor(ui.toast, 12, 0);
    lv_obj_set_style_pad_ver(ui.toast, 5, 0);
    ui.toast_label = label(ui.toast, cl_font_small, COL_TEXT, "");
    lv_obj_align(ui.toast, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);

    lv_screen_load(ui.screen);
}

void codeloom_ui_log_memory(void)
{
    lv_mem_monitor_t monitor;

    lv_mem_monitor(&monitor);
    ESP_LOGI("cl_ui", "lvgl pool: used=%u max_used=%u total=%u frag=%u%%",
             (unsigned)(monitor.total_size - monitor.free_size), (unsigned)monitor.max_used,
             (unsigned)monitor.total_size, (unsigned)monitor.frag_pct);
}

void codeloom_ui_set_battery(int soc)
{
    const char *icon;
    char text[24];

    if (soc < 0) {
        lv_obj_add_flag(ui.battery, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    icon = soc >= 80   ? LV_SYMBOL_BATTERY_FULL
           : soc >= 55 ? LV_SYMBOL_BATTERY_3
           : soc >= 30 ? LV_SYMBOL_BATTERY_2
           : soc >= 10 ? LV_SYMBOL_BATTERY_1
                       : LV_SYMBOL_BATTERY_EMPTY;
    snprintf(text, sizeof(text), "%s %d%%", icon, soc);
    lv_label_set_text(ui.battery, text);
    lv_obj_set_style_text_color(ui.battery, lv_color_hex(soc < 20 ? COL_RED : COL_MUTED), 0);
    lv_obj_align(ui.battery, LV_ALIGN_TOP_RIGHT, -20, 16);
    lv_obj_remove_flag(ui.battery, LV_OBJ_FLAG_HIDDEN);
}

static void set_header(const char *title, const char *subtitle)
{
    set_text(ui.title, title);
    set_text(ui.subtitle, subtitle);
    lv_obj_update_layout(ui.title);
    lv_obj_align_to(ui.subtitle, ui.title, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -4);
}

static void set_page_bars(int active)
{
    for (int i = 0; i < CL_PAGE_COUNT; ++i) {
        if (active < 0) {
            lv_obj_add_flag(ui.bars[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(ui.bars[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ui.bars[i],
                                  lv_color_hex(i == active ? COL_ACCENT : COL_BORDER), 0);
    }
}

static void set_body_area(int top, int bottom)
{
    ui.body_top = top;
    ui.body_h = bottom - top;
    lv_obj_set_pos(ui.body, 0, top);
    lv_obj_set_size(ui.body, BSP_LCD_W, bottom - top);
}

static void clear_body(void)
{
    lv_obj_clean(ui.body);
    ui.list = NULL;
    ui.card_count = 0;
    memset(ui.cards, 0, sizeof(ui.cards));
    memset(ui.card_meta, 0, sizeof(ui.card_meta));
    memset(ui.status_values, 0, sizeof(ui.status_values));
    memset(ui.buttons, 0, sizeof(ui.buttons));
    memset(ui.button_labels, 0, sizeof(ui.button_labels));
    memset(ui.confirm_buttons, 0, sizeof(ui.confirm_buttons));
    ui.detail_chip = ui.detail_age = ui.detail_box = ui.detail_title = NULL;
    ui.detail_text = ui.detail_line = ui.detail_page = NULL;
}

static lv_obj_t *scroll_list(void)
{
    lv_obj_t *list = box(ui.body);
    lv_obj_set_size(list, BSP_LCD_W, lv_pct(100));
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_style_pad_bottom(list, 6, 0);
    return list;
}

static void centered_message(const char *icon, const char *line1, const char *line2)
{
    lv_obj_t *column = box(ui.body);
    lv_obj_set_size(column, CARD_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, 6, 0);
    lv_obj_align(column, LV_ALIGN_CENTER, 0, -10);
    if (icon != NULL) {
        lv_obj_t *mark = label(column, cl_font_title, COL_ACCENT, "");
        lv_label_set_text(mark, icon);
    }
    label(column, cl_font_body, COL_TEXT, line1);
    if (line2 != NULL) {
        lv_obj_t *hint = label(column, cl_font_small, COL_MUTED, line2);
        lv_obj_set_width(hint, CARD_W);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    }
}

// ---------------------------------------------------------------------------
// 文案映射

static const char *kind_name(cl_kind_t kind)
{
    switch (kind) {
    case CL_KIND_SHELL:
        return "命令";
    case CL_KIND_FILE_WRITE:
        return "写文件";
    case CL_KIND_TOOL:
        return "工具";
    case CL_KIND_NETWORK:
        return "网络";
    default:
        return "其他";
    }
}

static uint32_t kind_color(cl_kind_t kind)
{
    switch (kind) {
    case CL_KIND_SHELL:
        return COL_AMBER;
    case CL_KIND_FILE_WRITE:
        return COL_CYAN;
    case CL_KIND_TOOL:
        return COL_ACCENT;
    case CL_KIND_NETWORK:
        return COL_GREEN;
    default:
        return COL_GRAY;
    }
}

static const char *task_status_name(cl_task_status_t status)
{
    switch (status) {
    case CL_TASK_BACKLOG:
        return "积压";
    case CL_TASK_TODO:
        return "待办";
    case CL_TASK_IN_PROGRESS:
        return "进行中";
    case CL_TASK_NEEDS_REVIEW:
        return "待评审";
    case CL_TASK_DONE:
        return "已完成";
    case CL_TASK_CANCELED:
        return "已取消";
    default:
        return "未知";
    }
}

static const char *run_status_name(cl_run_status_t status)
{
    switch (status) {
    case CL_RUN_PENDING:
        return "排队中";
    case CL_RUN_ACTIVE:
        return "运行中";
    case CL_RUN_IDLE:
        return "空闲";
    case CL_RUN_WAITING_APPROVAL:
        return "等待审批";
    case CL_RUN_COMPLETED:
        return "运行完成";
    case CL_RUN_FAILED:
        return "运行失败";
    case CL_RUN_CANCELED:
        return "运行取消";
    case CL_RUN_LOST:
        return "运行丢失";
    case CL_RUN_UNKNOWN:
        return "未知";
    default:
        return NULL;
    }
}

static uint32_t run_status_color(cl_run_status_t status)
{
    switch (status) {
    case CL_RUN_WAITING_APPROVAL:
        return COL_AMBER;
    case CL_RUN_ACTIVE:
        return COL_GREEN;
    case CL_RUN_FAILED:
    case CL_RUN_LOST:
        return COL_RED;
    default:
        return COL_GRAY;
    }
}

static const char *decision_label(cl_decision_t decision)
{
    switch (decision) {
    case CL_DECISION_ALLOW:
        return "批准";
    case CL_DECISION_ALLOW_ALWAYS:
        return "本次总是允许";
    default:
        return "拒绝";
    }
}

static uint32_t decision_color(cl_decision_t decision)
{
    switch (decision) {
    case CL_DECISION_ALLOW:
        return COL_GREEN;
    case CL_DECISION_ALLOW_ALWAYS:
        return COL_ACCENT;
    default:
        return COL_RED;
    }
}

static void format_age(char *dst, size_t size, int64_t age_s)
{
    if (age_s < 0) {
        dst[0] = '\0';
    } else {
        cl_format_age(dst, size, age_s);
    }
}

// ---------------------------------------------------------------------------
// 审批列表

static void build_approvals(const cl_state_t *state)
{
    uint8_t visible[CL_MAX_APPROVALS];
    uint8_t count = cl_state_visible_approvals(state, visible);
    int32_t small_lh = line_height(cl_font_small);
    int32_t body_lh = line_height(cl_font_body);
    int32_t chip_h = small_lh + 2;
    int32_t title_y = 8 + chip_h + 4;
    int32_t task_y = title_y + body_lh;
    int32_t card_h = task_y + body_lh + 8;

    if (!state->has_overview) {
        centered_message(NULL, "正在同步…", "首次同步完成后显示待审批");
        return;
    }
    if (count == 0) {
        centered_message(LV_SYMBOL_OK, "暂无待审批", "新请求会自动出现并响铃提醒");
        return;
    }
    ui.list = scroll_list();
    for (uint8_t i = 0; i < count; ++i) {
        const cl_approval_t *approval = &state->overview.approvals[visible[i]];
        lv_obj_t *card = box(ui.list);
        lv_obj_t *title;
        lv_obj_t *task;

        lv_obj_add_style(card, &st_card, 0);
        lv_obj_add_style(card, &st_card_selected, LV_STATE_CHECKED);
        lv_obj_set_size(card, CARD_W, card_h);

        lv_obj_t *kind = chip(card, kind_name(approval->kind), kind_color(approval->kind),
                              COL_BG, false);
        lv_obj_set_pos(kind, 10, 8);

        ui.card_meta[i] = label(card, cl_font_small, COL_MUTED, "");
        lv_obj_align(ui.card_meta[i], LV_ALIGN_TOP_RIGHT, -10, 8);

        title = label(card, cl_font_body, COL_TEXT, approval->title);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(title, CARD_W - 24, body_lh);
        lv_obj_set_pos(title, 10, title_y);

        task = label(card, cl_font_body, COL_MUTED,
                     approval->task_title[0] != '\0' ? approval->task_title : "—");
        lv_label_set_long_mode(task, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(task, CARD_W - 24, body_lh);
        lv_obj_set_pos(task, 10, task_y);

        ui.cards[i] = card;
    }
    ui.card_count = count;
    if (state->overview.approvals_total > state->overview.approval_count) {
        char note[64];
        lv_obj_t *more;
        snprintf(note, sizeof(note), "另有 %lu 条待审批未在设备上显示",
                 (unsigned long)(state->overview.approvals_total -
                                 state->overview.approval_count));
        more = label(ui.list, cl_font_small, COL_MUTED, note);
        lv_obj_set_style_pad_top(more, 2, 0);
    }
}

static void update_approvals(const cl_state_t *state, uint64_t now_ms)
{
    uint8_t visible[CL_MAX_APPROVALS];
    uint8_t count = cl_state_visible_approvals(state, visible);
    char text[32];

    for (uint8_t i = 0; i < ui.card_count && i < count; ++i) {
        const cl_approval_t *approval = &state->overview.approvals[visible[i]];
        bool sending = cl_state_is_inflight(state, approval->id);

        if (sending) {
            set_text(ui.card_meta[i], "发送中…");
        } else {
            format_age(text, sizeof(text), cl_state_approval_age_s(state, approval, now_ms));
            set_text(ui.card_meta[i], text);
        }
        lv_obj_set_style_text_color(ui.card_meta[i],
                                    lv_color_hex(sending ? COL_ACCENT : COL_MUTED), 0);
        lv_obj_align(ui.card_meta[i], LV_ALIGN_TOP_RIGHT, -10, 8);
        if (i == state->approval_sel) {
            lv_obj_add_state(ui.cards[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(ui.cards[i], LV_STATE_CHECKED);
        }
    }
    if (ui.card_count > 0 && state->approval_sel < ui.card_count) {
        lv_obj_scroll_to_view(ui.cards[state->approval_sel], LV_ANIM_ON);
    }
}

// ---------------------------------------------------------------------------
// 任务列表

static void build_tasks(const cl_state_t *state)
{
    const cl_overview_t *ov = &state->overview;
    int32_t body_lh = line_height(cl_font_body);
    int32_t chip_h = line_height(cl_font_small) + 2;
    unsigned active = 0;
    unsigned waiting = 0;
    char summary[96];
    lv_obj_t *summary_label;

    if (!state->has_overview) {
        centered_message(NULL, "正在同步…", NULL);
        return;
    }
    if (ov->task_count == 0) {
        centered_message(LV_SYMBOL_LIST, "暂无任务", "在 Codeloom 中创建任务后会显示在这里");
        return;
    }
    for (uint8_t i = 0; i < ov->task_count; ++i) {
        if (ov->tasks[i].status == CL_TASK_IN_PROGRESS) {
            active++;
        }
        if (ov->tasks[i].run_status == CL_RUN_WAITING_APPROVAL) {
            waiting++;
        }
    }
    ui.list = scroll_list();
    snprintf(summary, sizeof(summary), "共 %lu 个 · 进行中 %u · 待审批 %u",
             (unsigned long)ov->tasks_total, active, waiting);
    summary_label = label(ui.list, cl_font_small, COL_MUTED, summary);
    lv_obj_set_width(summary_label, CARD_W);

    for (uint8_t i = 0; i < ov->task_count; ++i) {
        const cl_task_t *task = &ov->tasks[i];
        const char *run = run_status_name(task->run_status);
        lv_obj_t *card = box(ui.list);
        lv_obj_t *title;
        lv_obj_t *status_chip;
        bool muted = task->status == CL_TASK_DONE || task->status == CL_TASK_CANCELED;

        lv_obj_add_style(card, &st_card, 0);
        lv_obj_add_style(card, &st_card_selected, LV_STATE_CHECKED);
        lv_obj_set_size(card, CARD_W, 8 + body_lh + 4 + chip_h + 8);
        if (task->run_status == CL_RUN_WAITING_APPROVAL) {
            lv_obj_set_style_border_color(card, lv_color_hex(COL_AMBER), 0);
        }

        title = label(card, cl_font_body, muted ? COL_MUTED : COL_TEXT, task->title);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(title, CARD_W - 24, body_lh);
        lv_obj_set_pos(title, 10, 8);

        // 不用额外的 flex 容器（每张卡省一个对象）：运行状态标签对齐到任务状态标签右侧。
        status_chip = chip(card, task_status_name(task->status), COL_MUTED, COL_MUTED, true);
        lv_obj_set_pos(status_chip, 10, 8 + body_lh + 4);
        if (run != NULL) {
            uint32_t color = run_status_color(task->run_status);
            lv_obj_t *run_chip = chip(card, run, color, color == COL_GRAY ? COL_TEXT : COL_BG,
                                      false);
            lv_obj_update_layout(status_chip);
            lv_obj_align_to(run_chip, status_chip, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
        }
        ui.cards[i] = card;
    }
    ui.card_count = ov->task_count;
}

static void update_tasks(const cl_state_t *state)
{
    for (uint8_t i = 0; i < ui.card_count; ++i) {
        if (i == state->task_sel) {
            lv_obj_add_state(ui.cards[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(ui.cards[i], LV_STATE_CHECKED);
        }
    }
    if (ui.card_count > 0 && state->task_sel < ui.card_count) {
        lv_obj_scroll_to_view(ui.cards[state->task_sel], LV_ANIM_ON);
    }
}

// ---------------------------------------------------------------------------
// 状态页

static const char *const STATUS_KEYS[CL_STATUS_ROWS] = {
    "Wi-Fi", "IP 地址", "信号", "服务器", "工作区", "设备 ID", "固件", "上次同步", "空闲内存",
    "最低 / 最大块",
};

static void build_status(void)
{
    int32_t row_h = line_height(cl_font_body) + 8;
    lv_obj_t *reset;
    lv_obj_t *reset_label;

    ui.list = scroll_list();
    lv_obj_set_style_pad_row(ui.list, 2, 0);
    for (int i = 0; i < CL_STATUS_ROWS; ++i) {
        lv_obj_t *row = box(ui.list);
        lv_obj_t *key;
        lv_obj_set_size(row, CARD_W, row_h);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(COL_SURFACE_HI), 0);
        key = label(row, cl_font_small, COL_MUTED, STATUS_KEYS[i]);
        lv_obj_align(key, LV_ALIGN_LEFT_MID, 2, 0);
        ui.status_values[i] = label(row, cl_font_body, COL_TEXT, "");
        lv_label_set_long_mode(ui.status_values[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(ui.status_values[i], 128, line_height(cl_font_body));
        lv_obj_set_style_text_align(ui.status_values[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(ui.status_values[i], LV_ALIGN_RIGHT_MID, -2, 0);
    }
    reset = box(ui.list);
    lv_obj_add_style(reset, &st_button, 0);
    lv_obj_set_style_border_color(reset, lv_color_hex(COL_RED), 0);
    lv_obj_set_size(reset, CARD_W, 38);
    lv_obj_set_style_margin_top(reset, 10, 0);
    reset_label = label(reset, cl_font_body, COL_RED, "重置配置");
    lv_obj_center(reset_label);
}

static void update_status(const cl_state_t *state, const cl_status_info_t *info, uint64_t now_ms)
{
    char value[CL_WORKSPACE_MAX + 16];
    int32_t max_scroll;

    set_text(ui.status_values[0], info->ssid);
    lv_obj_set_style_text_color(ui.status_values[0],
                                lv_color_hex(info->wifi.connected ? COL_TEXT : COL_AMBER), 0);
    set_text(ui.status_values[1], info->wifi.connected ? info->wifi.ip : "未连接");
    if (info->wifi.connected) {
        snprintf(value, sizeof(value), "%d dBm", info->wifi.rssi);
    } else {
        snprintf(value, sizeof(value), "—");
    }
    set_text(ui.status_values[2], value);
    set_text(ui.status_values[3], info->server_host);
    set_text(ui.status_values[4], state->has_overview && state->overview.workspace_name[0] != '\0'
                                      ? state->overview.workspace_name
                                      : info->workspace);
    set_text(ui.status_values[5], info->device_id);
    set_text(ui.status_values[6], info->firmware);
    if (!state->has_overview) {
        snprintf(value, sizeof(value), "尚未同步");
    } else {
        uint64_t age = now_ms > state->synced_at_ms ? (now_ms - state->synced_at_ms) / 1000U : 0;
        if (age < 60) {
            snprintf(value, sizeof(value), "%u 秒前", (unsigned)age);
        } else {
            cl_format_age(value, sizeof(value), (int64_t)age);
        }
    }
    set_text(ui.status_values[7], value);
    snprintf(value, sizeof(value), "%lu KB", (unsigned long)(info->heap.free_bytes / 1024U));
    set_text(ui.status_values[8], value);
    snprintf(value, sizeof(value), "%lu / %lu KB", (unsigned long)(info->heap.min_free_bytes / 1024U),
             (unsigned long)(info->heap.largest_block / 1024U));
    set_text(ui.status_values[9], value);

    // 状态页没有选中项：UP/DOWN 在整页滚动范围内按比例移动，最后一格露出“重置配置”。
    lv_obj_update_layout(ui.list);
    max_scroll = lv_obj_get_scroll_y(ui.list) + lv_obj_get_scroll_bottom(ui.list);
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    lv_obj_scroll_to_y(ui.list, max_scroll * state->status_row / (CL_STATUS_ROWS - 1),
                       LV_ANIM_ON);
}

// ---------------------------------------------------------------------------
// 审批详情

static void build_detail(const cl_state_t *state)
{
    int32_t body_lh = line_height(cl_font_body);
    int32_t chip_h = line_height(cl_font_small) + 2;
    int32_t box_y = chip_h + 6;
    // 自下而上布局：按钮贴底，其上是任务/状态行，详情框占用剩余高度（横幅出现时会变矮）。
    int32_t buttons_y = ui.body_h -
                        (CL_DECISION_COUNT * BUTTON_H + (CL_DECISION_COUNT - 1) * BUTTON_GAP) - 2;
    int32_t line_y = buttons_y - 4 - body_lh;
    int32_t box_h = line_y - 4 - box_y;
    lv_obj_t *column;

    ui.detail_chip = chip(ui.body, kind_name(state->detail.kind), kind_color(state->detail.kind),
                          COL_BG, false);
    lv_obj_set_pos(ui.detail_chip, 16, 2);
    ui.detail_age = label(ui.body, cl_font_small, COL_MUTED, "");
    lv_obj_align(ui.detail_age, LV_ALIGN_TOP_RIGHT, -16, 3);

    ui.detail_box = box(ui.body);
    lv_obj_set_pos(ui.detail_box, 16, box_y);
    lv_obj_set_size(ui.detail_box, BSP_LCD_W - 32, box_h);
    lv_obj_set_style_bg_opa(ui.detail_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui.detail_box, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_radius(ui.detail_box, 8, 0);
    lv_obj_set_style_pad_all(ui.detail_box, 6, 0);
    lv_obj_add_flag(ui.detail_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui.detail_box, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(ui.detail_box, lv_color_hex(COL_MUTED), LV_PART_SCROLLBAR);
    lv_obj_set_style_width(ui.detail_box, 3, LV_PART_SCROLLBAR);

    column = box(ui.detail_box);
    lv_obj_set_size(column, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, 4, 0);
    ui.detail_title = label(column, cl_font_body, COL_TEXT, "");
    lv_obj_set_width(ui.detail_title, lv_pct(100));
    lv_label_set_long_mode(ui.detail_title, LV_LABEL_LONG_MODE_WRAP);
    ui.detail_text = label(column, cl_font_body, COL_MUTED, "");
    lv_obj_set_width(ui.detail_text, lv_pct(100));
    lv_label_set_long_mode(ui.detail_text, LV_LABEL_LONG_MODE_WRAP);

    ui.detail_line = label(ui.body, cl_font_body, COL_MUTED, "");
    lv_label_set_long_mode(ui.detail_line, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(ui.detail_line, BSP_LCD_W - 32 - 40, body_lh);
    lv_obj_set_pos(ui.detail_line, 16, line_y);
    ui.detail_page = label(ui.body, cl_font_small, COL_MUTED, "");
    lv_obj_align(ui.detail_page, LV_ALIGN_TOP_RIGHT, -16, line_y + 2);

    for (int i = 0; i < CL_DECISION_COUNT; ++i) {
        lv_obj_t *button = box(ui.body);
        lv_obj_add_style(button, &st_button, 0);
        lv_obj_set_size(button, BSP_LCD_W - 32, BUTTON_H);
        lv_obj_set_pos(button, 16, buttons_y + i * (BUTTON_H + BUTTON_GAP));
        ui.buttons[i] = button;
        ui.button_labels[i] = label(button, cl_font_body, COL_TEXT,
                                    decision_label((cl_decision_t)i));
        lv_obj_center(ui.button_labels[i]);
    }
}

static void update_detail(const cl_state_t *state, uint64_t now_ms)
{
    const cl_approval_t *detail = &state->detail;
    bool locked = state->detail_status != CL_DETAIL_IDLE &&
                  state->detail_status != CL_DETAIL_FAILED;
    char text[CL_TASK_TITLE_MAX + 32];
    uint32_t line_color = COL_MUTED;
    int32_t content_h;
    int32_t visible_h;
    int pages;
    int page;

    format_age(text, sizeof(text), cl_state_approval_age_s(state, detail, now_ms));
    set_text(ui.detail_age, text);
    lv_obj_align(ui.detail_age, LV_ALIGN_TOP_RIGHT, -16, 3);
    set_text(ui.detail_title, detail->title);
    set_text(ui.detail_text, detail->detail[0] != '\0' ? detail->detail : "（无更多详情）");

    switch (state->detail_status) {
    case CL_DETAIL_SENDING:
        snprintf(text, sizeof(text), "正在提交「%s」…", decision_label(state->detail_decision));
        line_color = COL_ACCENT;
        break;
    case CL_DETAIL_DONE:
        snprintf(text, sizeof(text), "%s",
                 state->detail_decision == CL_DECISION_DENY           ? "已拒绝"
                 : state->detail_decision == CL_DECISION_ALLOW_ALWAYS ? "已批准（总是允许）"
                                                                      : "已批准");
        line_color = state->detail_decision == CL_DECISION_DENY ? COL_RED : COL_GREEN;
        break;
    case CL_DETAIL_ELSEWHERE:
        snprintf(text, sizeof(text), "已在别处处理");
        line_color = COL_AMBER;
        break;
    case CL_DETAIL_FAILED:
        snprintf(text, sizeof(text), "提交失败，按 OK 重试");
        line_color = COL_RED;
        break;
    case CL_DETAIL_REVOKED:
        snprintf(text, sizeof(text), "设备已吊销，无法提交");
        line_color = COL_RED;
        break;
    default:
        snprintf(text, sizeof(text), "任务：%s",
                 detail->task_title[0] != '\0' ? detail->task_title : "—");
        break;
    }
    set_text(ui.detail_line, text);
    lv_obj_set_style_text_color(ui.detail_line, lv_color_hex(line_color), 0);

    for (int i = 0; i < CL_DECISION_COUNT; ++i) {
        uint32_t color = decision_color((cl_decision_t)i);
        bool focused = i == state->action_focus;
        lv_obj_set_style_border_color(ui.buttons[i], lv_color_hex(color), 0);
        lv_obj_set_style_bg_color(ui.buttons[i], lv_color_hex(focused ? color : COL_SURFACE), 0);
        lv_obj_set_style_text_color(ui.button_labels[i], lv_color_hex(focused ? COL_BG : color),
                                    0);
        lv_obj_set_style_opa(ui.buttons[i], locked && !focused ? LV_OPA_40 : LV_OPA_COVER, 0);
    }

    // 长按 DOWN 逐页滚动详情框，到末页后回到开头。
    lv_obj_update_layout(ui.detail_box);
    visible_h = lv_obj_get_content_height(ui.detail_box);
    content_h = lv_obj_get_height(lv_obj_get_child(ui.detail_box, 0));
    pages = visible_h > 0 && content_h > visible_h ? (content_h + visible_h - 1) / visible_h : 1;
    page = state->detail_scroll % pages;
    lv_obj_scroll_to_y(ui.detail_box, page * visible_h, LV_ANIM_ON);
    if (pages > 1) {
        snprintf(text, sizeof(text), "%d/%d", page + 1, pages);
        set_text(ui.detail_page, text);
    } else {
        set_text(ui.detail_page, "");
    }
    lv_obj_align(ui.detail_page, LV_ALIGN_TOP_RIGHT, -16, lv_obj_get_y(ui.detail_line) + 2);
}

// ---------------------------------------------------------------------------
// 重置确认

static void build_confirm(void)
{
    lv_obj_t *card = box(ui.body);
    lv_obj_t *text;
    static const char *const labels[2] = {"取消", "重置并重启"};

    lv_obj_add_style(card, &st_card, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_RED), 0);
    lv_obj_set_size(card, CARD_W, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);

    label(card, cl_font_title, COL_TEXT, "重置配置？");
    text = label(card, cl_font_body, COL_MUTED,
                 "清除 Wi-Fi 和配对信息，\n重启后进入设置模式。");
    lv_obj_set_width(text, lv_pct(100));
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);

    for (int i = 0; i < 2; ++i) {
        lv_obj_t *button = box(card);
        lv_obj_t *button_label;
        lv_obj_add_style(button, &st_button, 0);
        lv_obj_set_size(button, lv_pct(100), 34);
        button_label = label(button, cl_font_body, COL_TEXT, labels[i]);
        lv_obj_center(button_label);
        ui.confirm_buttons[i] = button;
    }
}

static void update_confirm(const cl_state_t *state)
{
    for (int i = 0; i < 2; ++i) {
        bool focused = (i == 1) == state->reset_confirm_focus;
        uint32_t color = i == 1 ? COL_RED : COL_ACCENT;
        lv_obj_t *button_label = lv_obj_get_child(ui.confirm_buttons[i], 0);
        lv_obj_set_style_border_color(ui.confirm_buttons[i], lv_color_hex(color), 0);
        lv_obj_set_style_bg_color(ui.confirm_buttons[i],
                                  lv_color_hex(focused ? color : COL_SURFACE), 0);
        lv_obj_set_style_text_color(button_label, lv_color_hex(focused ? COL_BG : color), 0);
    }
}

// ---------------------------------------------------------------------------
// 正常模式渲染入口

void codeloom_ui_main_show(void)
{
    ui.mode = MODE_MAIN;
    ui.built = false;
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
}

static void update_banner(const cl_state_t *state)
{
    const char *text = NULL;
    uint32_t color = COL_AMBER;

    switch (state->link) {
    case CL_LINK_WIFI_CONNECTING:
        text = LV_SYMBOL_WIFI " 连接 Wi-Fi…";
        break;
    case CL_LINK_UNREACHABLE:
        text = LV_SYMBOL_WARNING " 服务器不可达";
        color = COL_RED;
        break;
    case CL_LINK_REVOKED:
        text = "设备已吊销 · 请在状态页重置";
        color = COL_RED;
        break;
    default:
        break;
    }
    if (text == NULL) {
        lv_obj_add_flag(ui.banner, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_style_bg_color(ui.banner, lv_color_hex(color), 0);
    set_text(ui.banner_label, text);
    lv_obj_center(ui.banner_label);
    lv_obj_remove_flag(ui.banner, LV_OBJ_FLAG_HIDDEN);
}

static void update_toast(const cl_state_t *state)
{
    const char *text = NULL;

    if (state->toast == CL_TOAST_HANDLED_ELSEWHERE) {
        text = "该审批已在别处处理";
    } else if (state->toast == CL_TOAST_QUEUE_FULL) {
        text = "提交中的审批过多，请稍候";
    }
    if (text == NULL) {
        lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    set_text(ui.toast_label, text);
    lv_obj_remove_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.toast);
    lv_obj_align(ui.toast, LV_ALIGN_BOTTOM_MID, 0, -40);
}

static const char *footer_for(const cl_state_t *state)
{
    if (state->view == CL_VIEW_DETAIL) {
        return NULL;
    }
    if (state->view == CL_VIEW_RESET_CONFIRM) {
        return LV_SYMBOL_UP LV_SYMBOL_DOWN " 选择 · OK 确认";
    }
    switch (state->page) {
    case CL_PAGE_APPROVALS:
        return "OK 查看 · 长按 " LV_SYMBOL_UP " 切换页面";
    case CL_PAGE_TASKS:
        return LV_SYMBOL_UP LV_SYMBOL_DOWN " 浏览 · 长按 " LV_SYMBOL_UP " 切换页面";
    default:
        return "OK 重置配置 · 长按 " LV_SYMBOL_UP " 切换页面";
    }
}

void codeloom_ui_main_render(const cl_state_t *state, const cl_status_info_t *info,
                             uint64_t now_ms)
{
    uint8_t visible[CL_MAX_APPROVALS];
    uint8_t visible_count = cl_state_visible_approvals(state, visible);
    bool banner = state->link != CL_LINK_OK;
    int top = banner ? HEADER_H + BANNER_H + 6 : HEADER_H + 2;
    const char *footer = footer_for(state);
    int bottom = footer != NULL ? FOOTER_Y - 4 : SCREEN_BOTTOM;
    char subtitle[32] = "";
    bool rebuild;

    if (ui.mode != MODE_MAIN) {
        return;
    }
    rebuild = !ui.built || ui.page != state->page || ui.view != state->view ||
              ui.gen != state->overview_gen || ui.visible != visible_count ||
              ui.has_overview != state->has_overview || ui.body_top != top ||
              (state->view == CL_VIEW_DETAIL && strcmp(ui.detail_id, state->detail.id) != 0);

    update_banner(state);
    if (rebuild) {
        clear_body();
        set_body_area(top, bottom);
        if (state->view == CL_VIEW_DETAIL) {
            set_header("详情", "长按 OK 返回");
            set_page_bars(-1);
            build_detail(state);
        } else if (state->view == CL_VIEW_RESET_CONFIRM) {
            set_header("重置配置", "");
            set_page_bars(-1);
            build_confirm();
        } else {
            set_page_bars(state->page);
            if (state->page == CL_PAGE_APPROVALS) {
                build_approvals(state);
            } else if (state->page == CL_PAGE_TASKS) {
                build_tasks(state);
            } else {
                build_status();
            }
        }
        if (footer != NULL) {
            set_text(ui.footer, footer);
            lv_obj_remove_flag(ui.footer, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui.footer, LV_OBJ_FLAG_HIDDEN);
        }
        ui.built = true;
        ui.page = state->page;
        ui.view = state->view;
        ui.gen = state->overview_gen;
        ui.visible = visible_count;
        ui.has_overview = state->has_overview;
        snprintf(ui.detail_id, sizeof(ui.detail_id), "%s", state->detail.id);
    }

    if (state->view == CL_VIEW_LIST) {
        if (state->page == CL_PAGE_APPROVALS) {
            if (state->has_overview) {
                snprintf(subtitle, sizeof(subtitle), "%lu 待处理",
                         (unsigned long)state->overview.approvals_total);
            }
            set_header("审批", subtitle);
            update_approvals(state, now_ms);
        } else if (state->page == CL_PAGE_TASKS) {
            if (state->has_overview) {
                snprintf(subtitle, sizeof(subtitle), "%lu 个",
                         (unsigned long)state->overview.tasks_total);
            }
            set_header("任务", subtitle);
            update_tasks(state);
        } else {
            set_header("状态", "");
            update_status(state, info, now_ms);
        }
    } else if (state->view == CL_VIEW_DETAIL) {
        update_detail(state, now_ms);
    } else {
        update_confirm(state);
    }
    update_toast(state);
}

// ---------------------------------------------------------------------------
// 配网模式

static void setup_step(lv_obj_t *parent, const char *number, const char *text)
{
    lv_obj_t *row = box(parent);
    lv_obj_t *badge;
    lv_obj_t *digit;
    lv_obj_t *caption;

    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    badge = box(row);
    lv_obj_set_size(badge, 18, 18);
    lv_obj_set_style_radius(badge, 9, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(COL_ACCENT), 0);
    digit = label(badge, cl_font_small, COL_BG, number);
    lv_obj_center(digit);
    caption = label(row, cl_font_small, COL_MUTED, text);
    lv_obj_set_width(caption, CARD_W - 24 - 24);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(caption, 24, 0);
}

static void enter_setup_mode(void)
{
    ui.mode = MODE_SETUP;
    ui.built = false;
    lv_obj_add_flag(ui.banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    set_page_bars(-1);
    clear_body();
}

void codeloom_ui_setup_show_ap(const char *ap_ssid, const char *ap_password, const char *url,
                               const char *status_line, const char *error_line)
{
    lv_obj_t *card;
    lv_obj_t *value;
    lv_obj_t *row;

    enter_setup_mode();
    set_header("Codeloom", "设置");
    set_body_area(HEADER_H, FOOTER_Y - 4);

    card = box(ui.body);
    lv_obj_add_style(card, &st_card, 0);
    lv_obj_set_size(card, CARD_W, LV_SIZE_CONTENT);
    lv_obj_set_pos(card, SIDE, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 4, 0);

    setup_step(card, "1", "手机连接 Wi-Fi 热点");
    value = label(card, cl_font_title, COL_TEXT, ap_ssid);
    lv_obj_set_style_pad_left(value, 24, 0);
    row = box(card);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 24, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    label(row, cl_font_small, COL_MUTED, "密码");
    value = label(row, cl_font_title, COL_AMBER, ap_password);
    lv_obj_set_style_text_letter_space(value, 2, 0);
    setup_step(card, "2", "浏览器打开");
    value = label(card, cl_font_body, COL_ACCENT, url);
    lv_obj_set_style_pad_left(value, 24, 0);
    setup_step(card, "3", "填写 Wi-Fi 与配对信息");

    if (error_line != NULL) {
        lv_obj_t *error = label(ui.body, cl_font_small, COL_RED, "");
        char text[96];
        snprintf(text, sizeof(text), "上次失败：%s", error_line);
        set_text(error, text);
        lv_obj_set_width(error, CARD_W);
        lv_label_set_long_mode(error, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_update_layout(card);
        lv_obj_align_to(error, card, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    }

    set_text(ui.footer, status_line);
    lv_obj_remove_flag(ui.footer, LV_OBJ_FLAG_HIDDEN);
}

void codeloom_ui_setup_show_progress(cl_setup_stage_t stage, const char *title,
                                     const char *detail)
{
    lv_obj_t *column;
    lv_obj_t *text;
    uint32_t color = stage == CL_SETUP_STAGE_FAILED    ? COL_RED
                     : stage == CL_SETUP_STAGE_SUCCESS ? COL_GREEN
                                                       : COL_TEXT;

    enter_setup_mode();
    set_header("Codeloom", "设置");
    set_body_area(HEADER_H, FOOTER_Y - 4);

    column = box(ui.body);
    lv_obj_set_size(column, CARD_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, 10, 0);
    lv_obj_align(column, LV_ALIGN_CENTER, 0, -8);

    if (stage == CL_SETUP_STAGE_WORKING) {
        lv_obj_t *spinner = lv_spinner_create(column);
        lv_obj_set_size(spinner, 44, 44);
        lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(COL_BORDER), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    } else {
        lv_obj_t *mark = label(column, cl_font_title, color, "");
        lv_label_set_text(mark, stage == CL_SETUP_STAGE_SUCCESS ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    }
    label(column, cl_font_title, color, title);
    text = label(column, cl_font_body, COL_MUTED, detail != NULL ? detail : "");
    lv_obj_set_width(text, CARD_W);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);

    if (stage == CL_SETUP_STAGE_FAILED) {
        set_text(ui.footer, "按 OK 返回设置");
        lv_obj_remove_flag(ui.footer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.footer, LV_OBJ_FLAG_HIDDEN);
    }
}
