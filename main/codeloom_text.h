// main/codeloom_text.h — UTF-8、JSON 字符串转义、ISO-8601 时间和相对时间格式化。
// 纯 C，无分配，供解析器、配网表单、UI 和主机测试共用。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 解码 data[0..length) 开头的一个 UTF-8 字符。合法时返回 true，并写出码点和字节宽度。
// 拒绝过长编码、代理区码点和超过 U+10FFFF 的值。
bool cl_utf8_decode(const char *data, size_t length, uint32_t *codepoint, size_t *width);

// 整段是否为合法 UTF-8。
bool cl_utf8_valid(const char *data, size_t length);

// 把 src[0..src_len) 复制到 dst（容量 dst_size，含 NUL），保证输出是合法 UTF-8：
// 非法字节替换为 '?'，控制字符（含换行、制表）折叠为单个空格，并去掉首尾空白。
// 放不下时在字符边界截断并追加 "…"（U+2026）。返回 true 表示发生了截断。
// dst_size 为 0 时什么也不做并返回 true。
bool cl_utf8_copy_sanitized(char *dst, size_t dst_size, const char *src, size_t src_len);

// 在 JSON 字符串字面量中转义 src，结果带两侧引号写入 dst。空间不足返回 -1，
// 否则返回写入的字节数（不含 NUL）。仅用于生成本应用的小型请求体。
int cl_json_quote(char *dst, size_t dst_size, const char *src);

// 解析 "YYYY-MM-DDTHH:MM:SS[.fff](Z|±HH:MM)" 为 Unix 秒（UTC）。格式不符返回 false。
bool cl_parse_iso8601(const char *text, int64_t *epoch_seconds);

// 把经过的秒数格式化为简短中文：“刚刚”“5分钟前”“2小时前”“3天前”。负数按 0 处理。
void cl_format_age(char *dst, size_t dst_size, int64_t seconds);
