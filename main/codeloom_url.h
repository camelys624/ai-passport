// main/codeloom_url.h — Codeloom 服务器地址的校验、规范化和接口 URL 拼接（纯 C）。
//
// 接受 "http://host[:port][/prefix]" 或 "https://..."：
//   - 协议和主机名不区分大小写，规范化为小写；
//   - 主机是 DNS 名称、IPv4 或方括号 IPv6；拒绝用户信息（@）、查询串、片段和空白；
//   - 端口 1..65535，可省略；
//   - 可选路径前缀只允许 URL 非保留字符和 '/'，末尾斜杠会去掉（协议要求无尾斜杠）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "codeloom_types.h"

#define CL_HOST_MAX 64

typedef struct {
    bool https;
    char host[CL_HOST_MAX];
    uint16_t port;              // 未写端口时为协议默认值（80/443）
    bool explicit_port;
    char normalized[CL_URL_MAX]; // 规范化后的基础地址，无尾斜杠
} cl_url_t;

// 解析并规范化 input（两侧 ASCII 空白会被忽略）。失败返回 false，out 内容未定义。
bool cl_url_parse(const char *input, cl_url_t *out);

// 用规范化基础地址和以 '/' 开头的 path 拼出完整 URL。空间不足或参数非法返回 false。
bool cl_url_join(char *dst, size_t dst_size, const char *base, const char *path);

// 生成用于状态页的简短显示 "host[:port]"。
void cl_url_display_host(const cl_url_t *url, char *dst, size_t dst_size);

// 审批/设备 ID 是否只含 [A-Za-z0-9_-]、非空且短于 CL_ID_MAX。不接受 '.'、'/'、'%'，
// 因此拼进 URL 路径时不会产生 ".." 或额外路径段。
bool cl_id_is_safe(const char *id);
