// main/codeloom_text.c — 见 codeloom_text.h。
#include "codeloom_text.h"

#include <stdio.h>
#include <string.h>

bool cl_utf8_decode(const char *data, size_t length, uint32_t *codepoint, size_t *width)
{
    const unsigned char *s = (const unsigned char *)data;
    uint32_t cp;
    size_t need;

    if (data == NULL || length == 0) {
        return false;
    }
    if (s[0] < 0x80U) {
        cp = s[0];
        need = 1;
    } else if (s[0] >= 0xC2U && s[0] <= 0xDFU) {
        cp = s[0] & 0x1FU;
        need = 2;
    } else if (s[0] >= 0xE0U && s[0] <= 0xEFU) {
        cp = s[0] & 0x0FU;
        need = 3;
    } else if (s[0] >= 0xF0U && s[0] <= 0xF4U) {
        cp = s[0] & 0x07U;
        need = 4;
    } else {
        return false;
    }
    if (length < need) {
        return false;
    }
    for (size_t i = 1; i < need; ++i) {
        if ((s[i] & 0xC0U) != 0x80U) {
            return false;
        }
        cp = (cp << 6) | (s[i] & 0x3FU);
    }
    // 过长编码、UTF-16 代理区和超范围码点都视为非法。
    if ((need == 3 && cp < 0x800U) || (need == 4 && (cp < 0x10000U || cp > 0x10FFFFU)) ||
        (cp >= 0xD800U && cp <= 0xDFFFU)) {
        return false;
    }
    if (codepoint != NULL) {
        *codepoint = cp;
    }
    if (width != NULL) {
        *width = need;
    }
    return true;
}

bool cl_utf8_valid(const char *data, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        size_t width;
        if (!cl_utf8_decode(data + offset, length - offset, NULL, &width)) {
            return false;
        }
        offset += width;
    }
    return true;
}

static bool is_collapsible_space(uint32_t cp)
{
    return cp < 0x20U || cp == 0x20U || cp == 0x7FU || (cp >= 0x80U && cp <= 0x9FU) ||
           cp == 0x2028U || cp == 0x2029U;
}

// 从 dst[0..len) 末尾退掉一个完整 UTF-8 字符（输出始终合法，只需跳过续字节）。
static size_t drop_last_char(const char *dst, size_t len)
{
    if (len == 0) {
        return 0;
    }
    --len;
    while (len > 0 && (((unsigned char)dst[len]) & 0xC0U) == 0x80U) {
        --len;
    }
    return len;
}

bool cl_utf8_copy_sanitized(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    static const char ellipsis[] = "\xE2\x80\xA6";
    size_t limit;
    size_t out = 0;
    size_t in = 0;
    bool pending_space = false;
    bool truncated = false;

    if (dst == NULL || dst_size == 0) {
        return true;
    }
    limit = dst_size - 1;
    if (src == NULL) {
        src_len = 0;
    }
    while (in < src_len) {
        uint32_t cp;
        size_t width;
        const char *bytes = src + in;
        size_t out_width;

        if (!cl_utf8_decode(src + in, src_len - in, &cp, &width)) {
            cp = '?';
            width = 1;
            bytes = "?";
        }
        in += width;
        if (is_collapsible_space(cp)) {
            pending_space = out > 0;
            continue;
        }
        out_width = width + (pending_space ? 1U : 0U);
        if (out + out_width > limit) {
            truncated = true;
            break;
        }
        if (pending_space) {
            dst[out++] = ' ';
            pending_space = false;
        }
        memcpy(dst + out, bytes, width);
        out += width;
    }
    if (truncated && limit >= sizeof(ellipsis) - 1) {
        while (out > 0 && out + (sizeof(ellipsis) - 1) > limit) {
            out = drop_last_char(dst, out);
        }
        while (out > 0 && dst[out - 1] == ' ') {
            --out;
        }
        memcpy(dst + out, ellipsis, sizeof(ellipsis) - 1);
        out += sizeof(ellipsis) - 1;
    }
    dst[out] = '\0';
    return truncated;
}

int cl_json_quote(char *dst, size_t dst_size, const char *src)
{
    static const char hex[] = "0123456789abcdef";
    size_t out = 0;

    if (dst == NULL || dst_size == 0 || src == NULL) {
        return -1;
    }
#define CL_PUT(ch)                  \
    do {                            \
        if (out + 1 >= dst_size) {  \
            return -1;              \
        }                           \
        dst[out++] = (char)(ch);    \
    } while (0)
    CL_PUT('"');
    for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; ++p) {
        switch (*p) {
        case '"':
            CL_PUT('\\');
            CL_PUT('"');
            break;
        case '\\':
            CL_PUT('\\');
            CL_PUT('\\');
            break;
        case '\n':
            CL_PUT('\\');
            CL_PUT('n');
            break;
        case '\r':
            CL_PUT('\\');
            CL_PUT('r');
            break;
        case '\t':
            CL_PUT('\\');
            CL_PUT('t');
            break;
        default:
            if (*p < 0x20U) {
                CL_PUT('\\');
                CL_PUT('u');
                CL_PUT('0');
                CL_PUT('0');
                CL_PUT(hex[*p >> 4]);
                CL_PUT(hex[*p & 0x0FU]);
            } else {
                CL_PUT(*p);
            }
            break;
        }
    }
    CL_PUT('"');
#undef CL_PUT
    dst[out] = '\0';
    return (int)out;
}

static bool read_digits(const char **cursor, int count, int *value)
{
    int result = 0;

    for (int i = 0; i < count; ++i) {
        char c = (*cursor)[i];
        if (c < '0' || c > '9') {
            return false;
        }
        result = result * 10 + (c - '0');
    }
    *cursor += count;
    *value = result;
    return true;
}

static bool expect_char(const char **cursor, char expected)
{
    if (**cursor != expected) {
        return false;
    }
    ++*cursor;
    return true;
}

// Howard Hinnant 的 days_from_civil：公历日期到 1970-01-01 起的天数。
static int64_t days_from_civil(int year, int month, int day)
{
    int64_t y = (int64_t)year - (month <= 2 ? 1 : 0);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t mp = (month + 9) % 12;
    int64_t doy = (153 * mp + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : days[month - 1];
}

bool cl_parse_iso8601(const char *text, int64_t *epoch_seconds)
{
    const char *p = text;
    int year, month, day, hour, minute, second;
    int offset_seconds = 0;

    if (text == NULL || epoch_seconds == NULL) {
        return false;
    }
    if (!read_digits(&p, 4, &year) || !expect_char(&p, '-') || !read_digits(&p, 2, &month) ||
        !expect_char(&p, '-') || !read_digits(&p, 2, &day) || !expect_char(&p, 'T') ||
        !read_digits(&p, 2, &hour) || !expect_char(&p, ':') || !read_digits(&p, 2, &minute) ||
        !expect_char(&p, ':') || !read_digits(&p, 2, &second)) {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month) || hour > 23 ||
        minute > 59 || second > 60) {
        return false;
    }
    if (*p == '.') {
        ++p;
        if (*p < '0' || *p > '9') {
            return false;
        }
        while (*p >= '0' && *p <= '9') {
            ++p;
        }
    }
    if (*p == 'Z') {
        ++p;
    } else if (*p == '+' || *p == '-') {
        int sign = *p == '-' ? -1 : 1;
        int oh, om;
        ++p;
        if (!read_digits(&p, 2, &oh) || !expect_char(&p, ':') || !read_digits(&p, 2, &om) ||
            oh > 23 || om > 59) {
            return false;
        }
        offset_seconds = sign * (oh * 3600 + om * 60);
    } else {
        return false;
    }
    if (*p != '\0') {
        return false;
    }
    *epoch_seconds = days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 +
                     (second > 59 ? 59 : second) - offset_seconds;
    return true;
}

void cl_format_age(char *dst, size_t dst_size, int64_t seconds)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (seconds < 60) {
        snprintf(dst, dst_size, "刚刚");
    } else if (seconds < 3600) {
        snprintf(dst, dst_size, "%d分钟前", (int)(seconds / 60));
    } else if (seconds < 86400) {
        snprintf(dst, dst_size, "%d小时前", (int)(seconds / 3600));
    } else {
        int64_t days = seconds / 86400;
        snprintf(dst, dst_size, "%d天前", days > 999 ? 999 : (int)days);
    }
}
