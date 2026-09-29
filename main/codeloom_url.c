// main/codeloom_url.c — 见 codeloom_url.h。
#include "codeloom_url.h"

#include <stdio.h>
#include <string.h>

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool is_alnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool starts_with_ci(const char *text, size_t length, const char *prefix)
{
    size_t n = strlen(prefix);
    if (length < n) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (lower(text[i]) != prefix[i]) {
            return false;
        }
    }
    return true;
}

static bool dns_host_valid(const char *host, size_t length)
{
    size_t label = 0;

    if (length == 0 || host[0] == '.' || host[length - 1] == '.') {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        char c = host[i];
        if (c == '.') {
            if (label == 0 || host[i - 1] == '-') {
                return false;
            }
            label = 0;
            continue;
        }
        if (!is_alnum(c) && c != '-') {
            return false;
        }
        if (c == '-' && label == 0) {
            return false;
        }
        if (++label > 63) {
            return false;
        }
    }
    return host[length - 1] != '-';
}

static bool ipv6_host_valid(const char *host, size_t length)
{
    size_t colons = 0;

    if (length < 4 || host[0] != '[' || host[length - 1] != ']') {
        return false;
    }
    for (size_t i = 1; i + 1 < length; ++i) {
        char c = host[i];
        if (c == ':') {
            ++colons;
        } else if (!is_hex(c) && c != '.') {
            return false;
        }
    }
    return colons >= 2;
}

static bool path_char_valid(char c)
{
    return is_alnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
}

bool cl_url_parse(const char *input, cl_url_t *out)
{
    const char *start;
    const char *end;
    const char *host;
    const char *host_end;
    const char *cursor;
    size_t host_len;
    size_t path_len;
    uint32_t port = 0;
    int written;

    if (input == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    start = input;
    while (is_space(*start)) {
        ++start;
    }
    end = start + strlen(start);
    while (end > start && is_space(end[-1])) {
        --end;
    }
    if (starts_with_ci(start, (size_t)(end - start), "https://")) {
        out->https = true;
        host = start + 8;
    } else if (starts_with_ci(start, (size_t)(end - start), "http://")) {
        host = start + 7;
    } else {
        return false;
    }

    if (host < end && *host == '[') {
        host_end = memchr(host, ']', (size_t)(end - host));
        if (host_end == NULL) {
            return false;
        }
        ++host_end;
    } else {
        host_end = host;
        while (host_end < end && *host_end != ':' && *host_end != '/') {
            ++host_end;
        }
    }
    host_len = (size_t)(host_end - host);
    if (host_len == 0 || host_len >= sizeof(out->host)) {
        return false;
    }
    if (host[0] == '[' ? !ipv6_host_valid(host, host_len) : !dns_host_valid(host, host_len)) {
        return false;
    }
    for (size_t i = 0; i < host_len; ++i) {
        out->host[i] = lower(host[i]);
    }
    out->host[host_len] = '\0';

    cursor = host_end;
    if (cursor < end && *cursor == ':') {
        ++cursor;
        if (cursor >= end || *cursor < '0' || *cursor > '9') {
            return false;
        }
        while (cursor < end && *cursor >= '0' && *cursor <= '9') {
            port = port * 10U + (uint32_t)(*cursor - '0');
            if (port > 65535U) {
                return false;
            }
            ++cursor;
        }
        if (port == 0) {
            return false;
        }
        out->explicit_port = true;
    } else {
        port = out->https ? 443U : 80U;
    }
    out->port = (uint16_t)port;

    if (cursor < end && *cursor != '/') {
        return false; // 例如主机后直接跟 '?'、'#'、'@' 或其他字符
    }
    for (const char *p = cursor; p < end; ++p) {
        if (!path_char_valid(*p)) {
            return false;
        }
    }
    while (end > cursor && end[-1] == '/') {
        --end;
    }
    path_len = (size_t)(end - cursor);

    if (out->explicit_port) {
        written = snprintf(out->normalized, sizeof(out->normalized), "%s://%s:%u%.*s",
                           out->https ? "https" : "http", out->host, (unsigned)out->port,
                           (int)path_len, cursor);
    } else {
        written = snprintf(out->normalized, sizeof(out->normalized), "%s://%s%.*s",
                           out->https ? "https" : "http", out->host, (int)path_len, cursor);
    }
    return written > 0 && (size_t)written < sizeof(out->normalized);
}

bool cl_url_join(char *dst, size_t dst_size, const char *base, const char *path)
{
    int written;

    if (dst == NULL || dst_size == 0 || base == NULL || path == NULL || path[0] != '/') {
        return false;
    }
    written = snprintf(dst, dst_size, "%s%s", base, path);
    return written > 0 && (size_t)written < dst_size;
}

void cl_url_display_host(const cl_url_t *url, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (url == NULL) {
        dst[0] = '\0';
    } else if (url->explicit_port) {
        snprintf(dst, dst_size, "%s:%u", url->host, (unsigned)url->port);
    } else {
        snprintf(dst, dst_size, "%s", url->host);
    }
}

bool cl_id_is_safe(const char *id)
{
    size_t length = 0;

    if (id == NULL || id[0] == '\0') {
        return false;
    }
    for (; id[length] != '\0'; ++length) {
        char c = id[length];
        if (length + 1 >= CL_ID_MAX) {
            return false;
        }
        if (!is_alnum(c) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}
