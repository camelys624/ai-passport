// Host tests for UTF-8 sanitizing/truncation, JSON quoting, ISO-8601 parsing and age text.
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "codeloom_text.h"

static void test_utf8_decode(void)
{
    uint32_t cp;
    size_t width;

    assert(cl_utf8_decode("\xE4\xB8\xAD", 3, &cp, &width) && cp == 0x4E2D && width == 3);
    assert(cl_utf8_decode("\xF0\x9F\x98\x80", 4, &cp, &width) && cp == 0x1F600 && width == 4);
    assert(!cl_utf8_decode("\xC0\xAF", 2, &cp, &width));         // overlong '/'
    assert(!cl_utf8_decode("\xED\xA0\x80", 3, &cp, &width));     // surrogate
    assert(!cl_utf8_decode("\xE4\xB8", 2, &cp, &width));         // truncated
    assert(!cl_utf8_decode("\xF4\x90\x80\x80", 4, &cp, &width)); // > U+10FFFF
    assert(cl_utf8_valid("abc\xE4\xB8\xAD", 6));
    assert(!cl_utf8_valid("abc\xFF", 4));
}

static void test_sanitized_copy(void)
{
    char out[16];
    const char *chinese = "中文标题测试"; // 6 × 3 bytes

    // Control characters collapse to one space; edges are trimmed.
    assert(!cl_utf8_copy_sanitized(out, sizeof(out), "  a\n\n\tb\x01 ", 9));
    assert(strcmp(out, "a b") == 0);

    // Invalid bytes become '?'.
    assert(!cl_utf8_copy_sanitized(out, sizeof(out), "x\xFFy", 3));
    assert(strcmp(out, "x?y") == 0);

    // Truncation never splits a multibyte character and ends with U+2026.
    assert(cl_utf8_copy_sanitized(out, 10, chinese, strlen(chinese)));
    assert(strcmp(out, "中文\xE2\x80\xA6") == 0);
    assert(cl_utf8_valid(out, strlen(out)));

    // Exact fit is not truncated.
    assert(!cl_utf8_copy_sanitized(out, 10, "中文标", strlen("中文标")));
    assert(strcmp(out, "中文标") == 0);

    // Buffers too small for the ellipsis still cut on a boundary.
    assert(cl_utf8_copy_sanitized(out, 3, "abcdef", 6));
    assert(strcmp(out, "ab") == 0);
    assert(cl_utf8_copy_sanitized(out, 3, "中", 3));
    assert(strcmp(out, "") == 0);

    // Trailing space before the ellipsis is dropped.
    assert(cl_utf8_copy_sanitized(out, 8, "abc defghij", 11));
    assert(strcmp(out, "abc\xE2\x80\xA6") == 0);
}

static void test_json_quote(void)
{
    char out[64];

    assert(cl_json_quote(out, sizeof(out), "a\"b\\c\n\x01") == 17);
    assert(strcmp(out, "\"a\\\"b\\\\c\\n\\u0001\"") == 0);
    assert(cl_json_quote(out, 4, "abc") == -1);
    assert(cl_json_quote(out, 6, "abc") == 5);
    assert(strcmp(out, "\"abc\"") == 0);
}

static void test_iso8601(void)
{
    int64_t t;

    assert(cl_parse_iso8601("1970-01-01T00:00:00Z", &t) && t == 0);
    assert(cl_parse_iso8601("2026-09-29T10:00:00.000Z", &t) && t == 1790676000);
    assert(cl_parse_iso8601("2026-09-29T18:00:00+08:00", &t) && t == 1790676000);
    assert(cl_parse_iso8601("2024-02-29T00:00:00Z", &t) && t == 1709164800);
    assert(!cl_parse_iso8601("2025-02-29T00:00:00Z", &t));
    assert(!cl_parse_iso8601("2026-09-29 10:00:00Z", &t));
    assert(!cl_parse_iso8601("2026-09-29T10:00:00", &t));
    assert(!cl_parse_iso8601("2026-09-29T10:00:00Zjunk", &t));
    assert(!cl_parse_iso8601("2026-13-01T00:00:00Z", &t));
}

static void test_age(void)
{
    char out[32];

    cl_format_age(out, sizeof(out), -5);
    assert(strcmp(out, "刚刚") == 0);
    cl_format_age(out, sizeof(out), 59);
    assert(strcmp(out, "刚刚") == 0);
    cl_format_age(out, sizeof(out), 60);
    assert(strcmp(out, "1分钟前") == 0);
    cl_format_age(out, sizeof(out), 7200);
    assert(strcmp(out, "2小时前") == 0);
    cl_format_age(out, sizeof(out), 3 * 86400 + 5);
    assert(strcmp(out, "3天前") == 0);
}

int main(void)
{
    test_utf8_decode();
    test_sanitized_copy();
    test_json_quote();
    test_iso8601();
    test_age();
    return 0;
}
