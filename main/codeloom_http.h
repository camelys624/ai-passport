// main/codeloom_http.h — 有界 HTTP(S) 请求（esp_http_client + 证书包）。
//
// 只在工作任务中调用：DNS、TCP、TLS 握手和收发都会阻塞，最长约 timeout_ms × 若干阶段。
// 响应体写入调用方提供的缓冲；Content-Length 超过容量或实际数据超过容量时立即中止，
// 不会分配与响应等长的内存。Authorization 头与请求体中的机密不会写入日志。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef enum {
    CL_HTTP_GET = 0,
    CL_HTTP_POST,
} cl_http_method_t;

typedef struct {
    int status;        // HTTP 状态码；传输失败时为 0
    size_t length;     // 已写入 body 的字节数（不含补上的 NUL）
    bool too_large;    // 响应超过容量而被中止
    esp_err_t error;   // 传输层错误；ESP_OK 表示拿到了状态码
} cl_http_response_t;

// body 缓冲容量为 body_cap（必须 ≥ 1，最后一字节留给 NUL）。body 可为 NULL（丢弃响应）。
// bearer 可为 NULL；json_body 可为 NULL（GET）。
void codeloom_http_request(cl_http_method_t method, const char *url, const char *bearer,
                           const char *json_body, char *body, size_t body_cap,
                           int timeout_ms, cl_http_response_t *out);
