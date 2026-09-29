// main/codeloom_setup_form.c — 见 codeloom_setup_form.h。
#include "codeloom_setup_form.h"

#include <string.h>

#include "codeloom_text.h"
#include "codeloom_url.h"

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// 解码 [src, src + length) 到 out；out_size 含 NUL。
static cl_form_result_t decode_value(const char *src, size_t length, char *out, size_t out_size)
{
    size_t written = 0;

    for (size_t i = 0; i < length; ++i) {
        char c = src[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            int high;
            int low;
            if (i + 2 >= length) {
                return CL_FORM_MALFORMED; // 结尾处不完整的 %X / %
            }
            high = hex_value(src[i + 1]);
            low = hex_value(src[i + 2]);
            if (high < 0 || low < 0 || (high == 0 && low == 0)) {
                return CL_FORM_MALFORMED;
            }
            c = (char)((high << 4) | low);
            i += 2;
        }
        if (written + 1 >= out_size) {
            return CL_FORM_TOO_LONG;
        }
        out[written++] = c;
    }
    out[written] = '\0';
    return cl_utf8_valid(out, written) ? CL_FORM_OK : CL_FORM_MALFORMED;
}

cl_form_result_t cl_form_get(const char *body, size_t length, const char *key, char *out,
                             size_t out_size)
{
    size_t key_len;
    size_t pos = 0;

    if (body == NULL || key == NULL || out == NULL || out_size == 0) {
        return CL_FORM_MISSING;
    }
    out[0] = '\0';
    key_len = strlen(key);
    while (pos <= length) {
        const char *pair = body + pos;
        const char *amp = memchr(pair, '&', length - pos);
        size_t pair_len = amp != NULL ? (size_t)(amp - pair) : length - pos;
        const char *eq = memchr(pair, '=', pair_len);

        if (eq != NULL && (size_t)(eq - pair) == key_len && memcmp(pair, key, key_len) == 0) {
            return decode_value(eq + 1, pair_len - key_len - 1, out, out_size);
        }
        if (amp == NULL) {
            break;
        }
        pos += pair_len + 1;
    }
    return CL_FORM_MISSING;
}

bool cl_wifi_ssid_valid(const char *ssid)
{
    size_t length = ssid != NULL ? strlen(ssid) : 0;
    return length >= 1 && length <= CL_SSID_MAX - 1;
}

bool cl_wifi_password_valid(const char *password)
{
    size_t length;

    if (password == NULL) {
        return false;
    }
    length = strlen(password);
    if (length == 0) {
        return true; // 开放网络
    }
    if (length == 64) {
        for (size_t i = 0; i < length; ++i) {
            if (hex_value(password[i]) < 0) {
                return false;
            }
        }
        return true;
    }
    if (length < 8 || length > 63) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (password[i] < 0x20 || password[i] > 0x7E) {
            return false;
        }
    }
    return true;
}

bool cl_pairing_code_valid(const char *code)
{
    size_t length;

    if (code == NULL || strncmp(code, "pair_", 5) != 0) {
        return false;
    }
    length = strlen(code);
    if (length <= 5 || length >= CL_PAIRING_CODE_MAX) {
        return false;
    }
    for (size_t i = 5; i < length; ++i) {
        char c = code[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static void trim_in_place(char *text)
{
    size_t start = 0;
    size_t end = strlen(text);

    while (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' ||
           text[start] == '\n') {
        ++start;
    }
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    memmove(text, text + start, end - start);
    text[end - start] = '\0';
}

cl_setup_error_t cl_setup_form_parse(const char *body, size_t length, cl_setup_request_t *out)
{
    char url_text[CL_URL_MAX];
    cl_url_t url;
    cl_form_result_t result;

    if (out == NULL || body == NULL || length > CL_SETUP_FORM_BODY_MAX) {
        return CL_SETUP_BAD_FORM;
    }
    memset(out, 0, sizeof(*out));

    result = cl_form_get(body, length, "ssid", out->ssid, sizeof(out->ssid));
    if (result == CL_FORM_TOO_LONG) {
        return CL_SETUP_BAD_SSID;
    }
    if (result != CL_FORM_OK) {
        return CL_SETUP_BAD_FORM;
    }
    if (!cl_wifi_ssid_valid(out->ssid)) {
        return CL_SETUP_BAD_SSID;
    }

    result = cl_form_get(body, length, "password", out->password, sizeof(out->password));
    if (result == CL_FORM_TOO_LONG) {
        return CL_SETUP_BAD_PASSWORD;
    }
    if (result != CL_FORM_OK) {
        return CL_SETUP_BAD_FORM;
    }
    if (!cl_wifi_password_valid(out->password)) {
        return CL_SETUP_BAD_PASSWORD;
    }

    result = cl_form_get(body, length, "server", url_text, sizeof(url_text));
    if (result == CL_FORM_TOO_LONG) {
        return CL_SETUP_BAD_URL;
    }
    if (result != CL_FORM_OK) {
        return CL_SETUP_BAD_FORM;
    }
    if (!cl_url_parse(url_text, &url)) {
        return CL_SETUP_BAD_URL;
    }
    strcpy(out->server_url, url.normalized);

    result = cl_form_get(body, length, "code", out->pairing_code, sizeof(out->pairing_code));
    if (result == CL_FORM_TOO_LONG) {
        return CL_SETUP_BAD_CODE;
    }
    if (result != CL_FORM_OK) {
        return CL_SETUP_BAD_FORM;
    }
    trim_in_place(out->pairing_code);
    if (!cl_pairing_code_valid(out->pairing_code)) {
        return CL_SETUP_BAD_CODE;
    }
    return CL_SETUP_OK;
}
