// main/codeloom_http.c — 见 codeloom_http.h。
#include "codeloom_http.h"

#include <stdio.h>
#include <string.h>

#include "codeloom_types.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "cl_http";

void codeloom_http_request(cl_http_method_t method, const char *url, const char *bearer,
                           const char *json_body, char *body, size_t body_cap,
                           int timeout_ms, cl_http_response_t *out)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = method == CL_HTTP_POST ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = timeout_ms,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach, // 仅 https:// 使用
        .keep_alive_enable = false,
    };
    char auth[8 + CL_TOKEN_MAX];
    esp_http_client_handle_t client;
    int write_len = json_body != NULL ? (int)strlen(json_body) : 0;
    int64_t content_length;
    esp_err_t err;

    memset(out, 0, sizeof(*out));
    if (body != NULL && body_cap > 0) {
        body[0] = '\0';
    }
    client = esp_http_client_init(&config);
    if (client == NULL) {
        out->error = ESP_ERR_NO_MEM;
        return;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    if (json_body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    if (bearer != NULL) {
        snprintf(auth, sizeof(auth), "Bearer %s", bearer);
        esp_http_client_set_header(client, "Authorization", auth);
        memset(auth, 0, sizeof(auth)); // 客户端已复制；不在栈上残留令牌
    }

    err = esp_http_client_open(client, write_len);
    if (err != ESP_OK) {
        out->error = err;
        goto done;
    }
    if (write_len > 0 && esp_http_client_write(client, json_body, write_len) != write_len) {
        out->error = ESP_FAIL;
        goto done;
    }
    content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        out->error = ESP_FAIL;
        goto done;
    }
    out->status = esp_http_client_get_status_code(client);
    if (body == NULL || body_cap == 0) {
        goto done; // 调用方只关心状态码；关闭连接即可丢弃响应体
    }
    if (content_length > (int64_t)(body_cap - 1)) {
        out->too_large = true;
        goto done;
    }
    for (;;) {
        size_t room = body_cap - 1 - out->length;
        int n;
        if (room == 0) {
            char probe;
            // 缓冲已满：再读 1 字节确认是否还有剩余数据（分块传输没有 Content-Length）。
            n = esp_http_client_read(client, &probe, 1);
            if (n > 0) {
                out->too_large = true;
            } else if (n < 0) {
                out->error = ESP_FAIL;
            }
            break;
        }
        n = esp_http_client_read(client, body + out->length, (int)room);
        if (n < 0) {
            out->error = ESP_FAIL;
            break;
        }
        if (n == 0) {
            break;
        }
        out->length += (size_t)n;
    }
    body[out->length] = '\0';

done:
    if (out->error != ESP_OK) {
        ESP_LOGW(TAG, "请求失败: %s (errno %d)", esp_err_to_name(out->error),
                 esp_http_client_get_errno(client));
        out->status = 0;
    } else if (out->too_large) {
        ESP_LOGW(TAG, "响应超过 %u 字节上限，已中止", (unsigned)(body_cap - 1));
        out->length = 0;
        if (body != NULL) {
            body[0] = '\0';
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}
