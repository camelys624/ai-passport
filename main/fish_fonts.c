// main/fish_fonts.c — 见 fish_fonts.h。
#include "fish_fonts.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "fish_ui_charset.h"

LV_FONT_DECLARE(fish_font_16);
LV_FONT_DECLARE(fish_font_24);
LV_FONT_DECLARE(fish_font_40);

static const char *TAG = "fish_fonts";

// 生成的字体描述符为 const；按 LVGL 字体指南复制一份可写描述符来挂回退链。
static lv_font_t s_body;
static lv_font_t s_title;

const lv_font_t *fish_font_body = &fish_font_16;
const lv_font_t *fish_font_title = &fish_font_24;
const lv_font_t *fish_font_large = &fish_font_40;

void fish_fonts_init(void)
{
    s_body = fish_font_16;
    s_body.fallback = &lv_font_montserrat_14;
    s_title = fish_font_24;
    s_title.fallback = &lv_font_montserrat_20;
    fish_font_body = &s_body;
    fish_font_title = &s_title;
}

static bool font_has(const lv_font_t *font, uint32_t codepoint)
{
    lv_font_glyph_dsc_t glyph;
    return lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) && !glyph.is_placeholder;
}

// 严格 UTF-8 解码一个码点；非法序列返回 0 字节宽度。
static size_t utf8_decode(const char *s, size_t length, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t width;
    uint32_t value;

    if (p[0] < 0x80) {
        *cp = p[0];
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0) {
        width = 2;
        value = p[0] & 0x1F;
    } else if ((p[0] & 0xF0) == 0xE0) {
        width = 3;
        value = p[0] & 0x0F;
    } else if ((p[0] & 0xF8) == 0xF0) {
        width = 4;
        value = p[0] & 0x07;
    } else {
        return 0;
    }
    if (width > length) {
        return 0;
    }
    for (size_t i = 1; i < width; ++i) {
        if ((p[i] & 0xC0) != 0x80) {
            return 0;
        }
        value = (value << 6) | (p[i] & 0x3F);
    }
    *cp = value;
    return width;
}

static int check_font(const char *name, const lv_font_t *font, const char *text)
{
    size_t length = strlen(text);
    size_t in = 0;
    int missing = 0;

    while (in < length) {
        uint32_t cp;
        size_t width = utf8_decode(text + in, length - in, &cp);
        if (width == 0) {
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

int fish_fonts_self_check(void)
{
    int missing = 0;

    missing += check_font("body", fish_font_body, FISH_UI_CHARSET);
    missing += check_font("title", fish_font_title, FISH_UI_CHARSET);
    missing += check_font("large", fish_font_large, FISH_LARGE_TEXT);
    missing += check_font("body", fish_font_body, LV_SYMBOL_BATTERY_FULL LV_SYMBOL_BATTERY_EMPTY);
    // 反例：U+9F98（龘）不在界面字符表中，必须报告缺失，证明检查不是恒真。
    if (font_has(fish_font_body, 0x9F98)) {
        ESP_LOGE(TAG, "字形检查失效：U+9F98 不应被覆盖");
        missing++;
    }
    if (missing == 0) {
        ESP_LOGI(TAG, "字形自检通过：%u 字节界面字符表", (unsigned)strlen(FISH_UI_CHARSET));
    }
    return missing;
}
