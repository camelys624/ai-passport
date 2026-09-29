// main/codeloom_fonts.c — 见 codeloom_fonts.h。
#include "codeloom_fonts.h"

#include <string.h>

#include "codeloom_text.h"
#include "codeloom_ui_charset.h"
#include "esp_log.h"

LV_FONT_DECLARE(codeloom_font_14);
LV_FONT_DECLARE(codeloom_font_16);
LV_FONT_DECLARE(codeloom_font_22);

static const char *TAG = "cl_fonts";

// 生成的字体描述符在 Flash 中且为 const；按 LVGL 字体指南复制一份可写描述符来挂回退链。
static lv_font_t s_small;
static lv_font_t s_body;
static lv_font_t s_title;

const lv_font_t *cl_font_small = &codeloom_font_14;
const lv_font_t *cl_font_body = &codeloom_font_16;
const lv_font_t *cl_font_title = &codeloom_font_22;

void codeloom_fonts_init(void)
{
    s_small = codeloom_font_14;
    s_small.fallback = &lv_font_montserrat_14;
    s_body = codeloom_font_16;
    s_body.fallback = &lv_font_montserrat_14;
    s_title = codeloom_font_22;
    s_title.fallback = &lv_font_montserrat_20;
    cl_font_small = &s_small;
    cl_font_body = &s_body;
    cl_font_title = &s_title;
}

static bool font_has(const lv_font_t *font, uint32_t codepoint)
{
    lv_font_glyph_dsc_t glyph;
    return lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) && !glyph.is_placeholder;
}

void codeloom_fonts_sanitize(const lv_font_t *font, const char *src, char *dst, size_t dst_size)
{
    static const char replacement[] = "\xE2\x96\xA1"; // U+25A1 □
    size_t in = 0;
    size_t out = 0;
    size_t length;

    if (dst == NULL || dst_size == 0) {
        return;
    }
    length = src != NULL ? strlen(src) : 0;
    while (in < length) {
        uint32_t cp;
        size_t width;
        const char *bytes;
        size_t out_width;

        if (!cl_utf8_decode(src + in, length - in, &cp, &width)) {
            bytes = "?";
            out_width = 1;
            width = 1;
        } else if (cp == '\n' || font_has(font, cp)) {
            bytes = src + in;
            out_width = width;
        } else {
            bytes = replacement;
            out_width = sizeof(replacement) - 1;
        }
        if (out + out_width >= dst_size) {
            break;
        }
        memcpy(dst + out, bytes, out_width);
        out += out_width;
        in += width;
    }
    dst[out] = '\0';
}

static int check_font(const char *name, const lv_font_t *font, const char *text)
{
    size_t length = strlen(text);
    size_t in = 0;
    int missing = 0;

    while (in < length) {
        uint32_t cp;
        size_t width;
        if (!cl_utf8_decode(text + in, length - in, &cp, &width)) {
            ESP_LOGE(TAG, "%s: 字符表不是合法 UTF-8", name);
            return missing + 1;
        }
        if (cp >= 0x20 && !font_has(font, cp)) {
            ESP_LOGE(TAG, "%s 缺字 U+%04lX", name, (unsigned long)cp);
            missing++;
        }
        in += width;
    }
    return missing;
}

int codeloom_fonts_self_check(void)
{
    // GB2312 一级汉字首尾（啊、座）、替换符和几个图标只要求正文字号覆盖。
    static const char body_extra[] = "啊座□" LV_SYMBOL_WIFI LV_SYMBOL_OK;
    int missing = 0;

    missing += check_font("small", cl_font_small, CL_UI_CHARSET);
    missing += check_font("body", cl_font_body, CL_UI_CHARSET);
    missing += check_font("title", cl_font_title, CL_UI_CHARSET);
    missing += check_font("body", cl_font_body, body_extra);
    missing += check_font("small", cl_font_small, LV_SYMBOL_BATTERY_FULL LV_SYMBOL_BATTERY_EMPTY);
    // 反例：U+9F98（龘）不在 GB2312 一级字表中，必须报告缺失，证明检查不是恒真。
    if (font_has(cl_font_body, 0x9F98)) {
        ESP_LOGE(TAG, "字形检查失效：U+9F98 不应被覆盖");
        missing++;
    }
    if (missing == 0) {
        ESP_LOGI(TAG, "字形自检通过：%u 字节界面字符表", (unsigned)strlen(CL_UI_CHARSET));
    }
    return missing;
}
