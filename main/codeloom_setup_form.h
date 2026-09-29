// main/codeloom_setup_form.h — 配网表单（application/x-www-form-urlencoded）的有界解码与校验。
// 纯 C；HTTP 层先按 CL_SETUP_FORM_BODY_MAX 限制正文，再交给本模块。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "codeloom_types.h"

#define CL_SETUP_FORM_BODY_MAX 1024

typedef enum {
    CL_FORM_OK = 0,
    CL_FORM_MISSING,   // 字段不存在
    CL_FORM_TOO_LONG,  // 解码后放不下
    CL_FORM_MALFORMED, // 百分号编码错误、%00 或非法 UTF-8
} cl_form_result_t;

// 在 body[0..length) 中查找 key 的第一个值，URL 解码（'+' → 空格）后写入 out。
cl_form_result_t cl_form_get(const char *body, size_t length, const char *key, char *out,
                             size_t out_size);

typedef enum {
    CL_SETUP_OK = 0,
    CL_SETUP_BAD_FORM,     // 字段缺失或编码错误
    CL_SETUP_BAD_SSID,     // 空或超过 32 字节
    CL_SETUP_BAD_PASSWORD, // 非空时须为 8..63 个可打印 ASCII，或 64 位十六进制
    CL_SETUP_BAD_URL,      // 不是合法的 http(s) 服务器地址
    CL_SETUP_BAD_CODE,     // 不是 "pair_" 开头的配对码
} cl_setup_error_t;

typedef struct {
    char ssid[CL_SSID_MAX];
    char password[CL_WIFI_PASSWORD_MAX];
    char server_url[CL_URL_MAX]; // 已规范化
    char pairing_code[CL_PAIRING_CODE_MAX];
} cl_setup_request_t;

// 解析字段 ssid、password、server、code。SSID 与口令保持原样（不去空白，Wi-Fi 允许空格），
// 服务器地址和配对码会去掉两侧空白。
cl_setup_error_t cl_setup_form_parse(const char *body, size_t length, cl_setup_request_t *out);

// 校验 Wi-Fi 字段（供表单和已保存配置共用）。
bool cl_wifi_ssid_valid(const char *ssid);
bool cl_wifi_password_valid(const char *password);

// 配对码：去空白后以 "pair_" 开头，其余为 [A-Za-z0-9_-]，总长 < CL_PAIRING_CODE_MAX。
bool cl_pairing_code_valid(const char *code);
