// main/codeloom_setup.c — 见 codeloom_setup.h。
//
// 资源顺序（phoenixzhc SoftAP 经验）：配网期间不初始化音频；AP 单客户端、HTTP 3 个
// socket、LRU 清理、6 KB 服务任务栈；表单正文限制 1 KB 且完整接收后才解码；
// 在 STA 连接和配对都成功之前不写 NVS。Wi-Fi 口令、配对码和设备令牌不会写入日志。
#include "codeloom_setup.h"

#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "codeloom_diag.h"
#include "codeloom_events.h"
#include "codeloom_http.h"
#include "codeloom_protocol.h"
#include "codeloom_setup_form.h"
#include "codeloom_store.h"
#include "codeloom_timing.h"
#include "codeloom_ui.h"
#include "codeloom_url.h"
#include "codeloom_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SETUP_URL "http://192.168.4.1/"
#define STA_TIMEOUT_MS 25000
#define PAIR_TIMEOUT_MS 10000
#define FAILURE_AUTO_RETURN_MS 60000
#define RESPONSE_FLUSH_MS 1500
#define PAIR_RESPONSE_MAX 2048

static const char *TAG = "cl_setup";

static char s_ap_ssid[16];
static char s_ap_password[9];
static char s_suffix[5];
static httpd_handle_t s_server;
static SemaphoreHandle_t s_lock;
static cl_setup_request_t s_pending;
static bool s_pending_ready;
static const char *s_last_error; // 上次失败原因（常量字符串，不含用户输入）
static char s_prefill_ssid[CL_SSID_MAX];
static char s_prefill_server[CL_URL_MAX];
static char s_form_body[CL_SETUP_FORM_BODY_MAX + 1];
static char s_pair_response[PAIR_RESPONSE_MAX];
static cl_backlight_t s_backlight;
static uint64_t s_battery_at_ms;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000U;
}

// ---------------------------------------------------------------------------
// 本地网页

static const char PAGE_HEAD[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Codeloom 设置</title><style>"
    "body{font-family:system-ui,sans-serif;background:#0e1116;color:#e9ecf2;margin:0;padding:20px}"
    "h1{font-size:22px;margin:0 0 4px}p{color:#8c94a3;font-size:14px;margin:4px 0 16px}"
    "label{display:block;font-size:14px;color:#8c94a3;margin:12px 0 4px}"
    "input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #2c3340;"
    "background:#181c24;color:#e9ecf2;font-size:16px}"
    "button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:10px;background:#6c8cff;"
    "color:#0e1116;font-size:16px;font-weight:600}"
    ".err{background:#3a1c1f;color:#ef5b5b;padding:10px;border-radius:8px;font-size:14px}"
    "</style></head><body><h1>Codeloom 设备设置</h1>"
    "<p>设备只支持 2.4 GHz Wi-Fi。配对码在 Codeloom 网页的“设备”中生成，10 分钟内有效。</p>";

static const char PAGE_FORM_1[] =
    "<form method=\"post\" action=\"/setup\">"
    "<label>Wi-Fi 名称</label><input name=\"ssid\" maxlength=\"32\" required value=\"";
static const char PAGE_FORM_2[] =
    "\"><label>Wi-Fi 密码</label><input name=\"password\" type=\"password\" maxlength=\"64\">"
    "<label>Codeloom 服务器地址</label>"
    "<input name=\"server\" type=\"url\" maxlength=\"120\" required "
    "placeholder=\"http://192.168.1.10:5181\" value=\"";
static const char PAGE_FORM_3[] =
    "\"><label>配对码</label><input name=\"code\" maxlength=\"90\" required "
    "placeholder=\"pair_…\" autocapitalize=\"off\" autocomplete=\"off\">"
    "<button type=\"submit\">保存并配对</button></form></body></html>";

static const char PAGE_ACCEPTED[] =
    "<h1>已收到</h1><p>设备正在连接 Wi-Fi 并配对，热点即将关闭。请查看设备屏幕。</p>"
    "<p>如果失败，设备会显示原因并重新开启热点，重新连接后刷新本页即可再试。</p>"
    "</body></html>";

// 只转义 HTML 属性值中有意义的字符。
static esp_err_t send_escaped(httpd_req_t *req, const char *text)
{
    char chunk[64];
    size_t used = 0;

    for (const char *p = text; *p != '\0'; ++p) {
        const char *rep = NULL;
        switch (*p) {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        case '\'':
            rep = "&#39;";
            break;
        default:
            break;
        }
        size_t need = rep != NULL ? strlen(rep) : 1;
        if (used + need >= sizeof(chunk)) {
            chunk[used] = '\0';
            if (httpd_resp_sendstr_chunk(req, chunk) != ESP_OK) {
                return ESP_FAIL;
            }
            used = 0;
        }
        if (rep != NULL) {
            memcpy(chunk + used, rep, need);
        } else {
            chunk[used] = *p;
        }
        used += need;
    }
    chunk[used] = '\0';
    return used > 0 ? httpd_resp_sendstr_chunk(req, chunk) : ESP_OK;
}

static esp_err_t send_form_page(httpd_req_t *req, const char *error)
{
    char ssid[CL_SSID_MAX];
    char server[CL_URL_MAX];

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(ssid, s_prefill_ssid, sizeof(ssid));
    memcpy(server, s_prefill_server, sizeof(server));
    if (error == NULL) {
        error = s_last_error;
    }
    xSemaphoreGive(s_lock);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (httpd_resp_sendstr_chunk(req, PAGE_HEAD) != ESP_OK) {
        return ESP_FAIL;
    }
    if (error != NULL) {
        httpd_resp_sendstr_chunk(req, "<div class=\"err\">");
        httpd_resp_sendstr_chunk(req, error); // 常量文案
        httpd_resp_sendstr_chunk(req, "</div>");
    }
    httpd_resp_sendstr_chunk(req, PAGE_FORM_1);
    send_escaped(req, ssid);
    httpd_resp_sendstr_chunk(req, PAGE_FORM_2);
    send_escaped(req, server);
    httpd_resp_sendstr_chunk(req, PAGE_FORM_3);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t handle_get(httpd_req_t *req)
{
    return send_form_page(req, NULL);
}

static const char *setup_error_text(cl_setup_error_t error)
{
    switch (error) {
    case CL_SETUP_BAD_SSID:
        return "Wi-Fi 名称不能为空，且不超过 32 字节。";
    case CL_SETUP_BAD_PASSWORD:
        return "Wi-Fi 密码需为 8–63 个字符（开放网络留空）。";
    case CL_SETUP_BAD_URL:
        return "服务器地址无效，例如 http://192.168.1.10:5181";
    case CL_SETUP_BAD_CODE:
        return "配对码应以 pair_ 开头。";
    default:
        return "表单内容无效，请重试。";
    }
}

static esp_err_t handle_post(httpd_req_t *req)
{
    size_t received = 0;
    cl_setup_request_t request;
    cl_setup_error_t error;
    int retries = 0;

    if (req->content_len > CL_SETUP_FORM_BODY_MAX) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Form too large");
        return ESP_FAIL;
    }
    while (received < req->content_len) {
        int n = httpd_req_recv(req, s_form_body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++retries <= 3) {
            continue;
        }
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Incomplete form");
            return ESP_FAIL;
        }
        received += (size_t)n;
    }
    s_form_body[received] = '\0';

    error = cl_setup_form_parse(s_form_body, received, &request);
    memset(s_form_body, 0, sizeof(s_form_body)); // 正文含 Wi-Fi 口令，用完即清
    if (error != CL_SETUP_OK) {
        ESP_LOGW(TAG, "表单校验失败: %d", (int)error);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_form_page(req, setup_error_text(error));
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_pending = request;
    s_pending_ready = true;
    snprintf(s_prefill_ssid, sizeof(s_prefill_ssid), "%s", request.ssid);
    snprintf(s_prefill_server, sizeof(s_prefill_server), "%s", request.server_url);
    xSemaphoreGive(s_lock);
    memset(&request, 0, sizeof(request));

    ESP_LOGI(TAG, "收到配网表单");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, PAGE_ACCEPTED);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    const httpd_uri_t post_uri = {.uri = "/setup", .method = HTTP_POST, .handler = handle_post};
    // 通配 GET：任意路径（含系统联网探测）都返回表单页。
    const httpd_uri_t get_uri = {.uri = "/*", .method = HTTP_GET, .handler = handle_get};
    esp_err_t err;

    config.max_open_sockets = 3;
    config.backlog_conn = 2;
    config.lru_purge_enable = true;
    config.stack_size = 6144;
    config.max_uri_handlers = 4;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    config.uri_match_fn = httpd_uri_match_wildcard;
    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务启动失败: %s", esp_err_to_name(err));
        return err;
    }
    httpd_register_uri_handler(s_server, &post_uri);
    httpd_register_uri_handler(s_server, &get_uri);
    return ESP_OK;
}

static void stop_server(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

// ---------------------------------------------------------------------------
// 按键 / 背光 / 电量：配网期间也遵守空闲变暗和熄屏

static void apply_backlight(void)
{
    bsp_display_backlight(cl_backlight_percent(s_backlight.screen));
}

// 处理最多 wait_ms 的事件；返回 true 表示有一次“有效”的 OK 单击（唤醒按键不算）。
static bool pump(uint32_t wait_ms)
{
    cl_event_t event;
    bool ok_clicked = false;
    uint64_t now;

    if (codeloom_events_receive(&event, wait_ms) && event.type == CL_EVENT_KEY) {
        now = now_ms();
        if (event.key.gesture == BSP_BTN_PRESS) {
            if (cl_backlight_press(&s_backlight, event.key.button, now)) {
                apply_backlight();
            }
        } else {
            bool changed = false;
            bool swallowed = cl_backlight_gesture_swallowed(&s_backlight, event.key.button, now,
                                                            &changed);
            if (changed) {
                apply_backlight();
            }
            ok_clicked = !swallowed && event.key.button == BSP_BTN_OK &&
                         event.key.gesture == BSP_BTN_CLICK;
        }
    }
    now = now_ms();
    if (cl_backlight_tick(&s_backlight, now)) {
        apply_backlight();
    }
    if (now - s_battery_at_ms >= 10000U || s_battery_at_ms == 0) {
        int soc = bsp_battery_soc();
        s_battery_at_ms = now;
        if (bsp_lvgl_lock(200)) {
            codeloom_ui_set_battery(soc);
            bsp_lvgl_unlock();
        }
    }
    return ok_clicked;
}

static void show_progress(cl_setup_stage_t stage, const char *title, const char *detail)
{
    cl_backlight_wake(&s_backlight, now_ms());
    apply_backlight();
    if (bsp_lvgl_lock(1000)) {
        codeloom_ui_setup_show_progress(stage, title, detail);
        bsp_lvgl_unlock();
    }
}

static void show_ap(const char *status)
{
    if (bsp_lvgl_lock(1000)) {
        codeloom_ui_setup_show_ap(s_ap_ssid, s_ap_password, SETUP_URL, status, s_last_error);
        bsp_lvgl_unlock();
    }
}

// ---------------------------------------------------------------------------
// 连接与配对

static const char *wifi_error_text(cl_wifi_result_t result)
{
    switch (result) {
    case CL_WIFI_RESULT_AUTH_FAILED:
        return "Wi-Fi 密码错误";
    case CL_WIFI_RESULT_NO_AP:
        return "找不到该 Wi-Fi（仅支持 2.4 GHz）";
    case CL_WIFI_RESULT_TIMEOUT:
        return "连接 Wi-Fi 超时";
    default:
        return "Wi-Fi 启动失败";
    }
}

// 成功时填充 config 的令牌/设备 ID/工作区，返回 NULL；失败返回原因文案。
static const char *pair_device(const cl_setup_request_t *request, cl_config_t *config)
{
    char url[CL_URL_MAX + 32];
    char body[256];
    char name[24];
    char firmware[CL_FIRMWARE_VERSION_MAX];
    char code[48];
    cl_http_response_t response;
    cl_pair_response_t pair;

    snprintf(name, sizeof(name), "Passport-%s", s_suffix);
    codeloom_firmware_version(firmware, sizeof(firmware));
    if (!cl_url_join(url, sizeof(url), request->server_url, "/api/v1/devices/pair") ||
        cl_build_pair_body(body, sizeof(body), request->pairing_code, name, firmware) < 0) {
        return "服务器地址或配对码过长";
    }
    codeloom_http_request(CL_HTTP_POST, url, NULL, body, s_pair_response,
                          sizeof(s_pair_response), PAIR_TIMEOUT_MS, &response);
    memset(body, 0, sizeof(body));
    codeloom_log_heap("after pair request");
    if (response.error != ESP_OK) {
        return "服务器不可达（检查地址、端口和网络）";
    }
    if (response.status >= 400 && response.status < 500) {
        if (cl_parse_error_code(s_pair_response, response.length, code, sizeof(code))) {
            ESP_LOGW(TAG, "配对被拒绝: HTTP %d %s", response.status, code);
        } else {
            ESP_LOGW(TAG, "配对被拒绝: HTTP %d", response.status);
        }
        return "配对码无效或已过期";
    }
    if (response.status != 200 || response.too_large) {
        ESP_LOGW(TAG, "配对失败: HTTP %d", response.status);
        return "服务器错误，请稍后重试";
    }
    if (cl_parse_pair_response(s_pair_response, response.length, &pair) != CL_PARSE_OK) {
        memset(s_pair_response, 0, sizeof(s_pair_response));
        return "服务器响应无效";
    }
    memset(s_pair_response, 0, sizeof(s_pair_response));
    snprintf(config->device_token, sizeof(config->device_token), "%s", pair.device_token);
    snprintf(config->device_id, sizeof(config->device_id), "%s", pair.device_id);
    snprintf(config->workspace_name, sizeof(config->workspace_name), "%s", pair.workspace_name);
    memset(&pair, 0, sizeof(pair));
    ESP_LOGI(TAG, "配对成功: %s", config->device_id);
    return NULL;
}

// 执行一次“连接 + 配对 + 保存”。成功时不返回（重启）；失败返回原因。
static const char *attempt(const cl_setup_request_t *request)
{
    cl_config_t config = {0};
    cl_wifi_result_t result;
    const char *error;
    char detail[CL_WORKSPACE_MAX + 32];

    show_progress(CL_SETUP_STAGE_WORKING, "正在连接 Wi-Fi", request->ssid);
    if (codeloom_wifi_start_sta(request->ssid, request->password, false) != ESP_OK) {
        return wifi_error_text(CL_WIFI_RESULT_ERROR);
    }
    result = codeloom_wifi_wait_sta(STA_TIMEOUT_MS);
    if (result != CL_WIFI_RESULT_CONNECTED) {
        return wifi_error_text(result);
    }
    codeloom_log_heap("after Wi-Fi up");

    show_progress(CL_SETUP_STAGE_WORKING, "正在配对", "连接 Codeloom 服务器…");
    error = pair_device(request, &config);
    if (error != NULL) {
        return error;
    }
    snprintf(config.ssid, sizeof(config.ssid), "%s", request->ssid);
    snprintf(config.password, sizeof(config.password), "%s", request->password);
    snprintf(config.server_url, sizeof(config.server_url), "%s", request->server_url);
    if (codeloom_store_save(&config) != CL_SETTINGS_OK) {
        memset(&config, 0, sizeof(config));
        return "保存配置失败";
    }
    snprintf(detail, sizeof(detail), "工作区：%s\n正在重启…",
             config.workspace_name[0] != '\0' ? config.workspace_name : "—");
    memset(&config, 0, sizeof(config));
    show_progress(CL_SETUP_STAGE_SUCCESS, "配对成功", detail);
    vTaskDelay(pdMS_TO_TICKS(2000));
    codeloom_wifi_stop();
    esp_restart();
    return NULL;
}

static void make_ap_credentials(void)
{
    uint32_t random = 0;

    codeloom_wifi_suffix(s_suffix);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Codeloom-%s", s_suffix);
    if (codeloom_wifi_random(&random, sizeof(random)) != ESP_OK) {
        ESP_LOGE(TAG, "无法获取射频随机数");
    }
    // 8 位数字口令（WPA2 最短 8 字符），显示在屏幕上，不写入日志。
    snprintf(s_ap_password, sizeof(s_ap_password), "%08lu",
             (unsigned long)(random % 100000000UL));
}

void codeloom_setup_task(void *arg)
{
    (void)arg;
    s_lock = xSemaphoreCreateMutex();
    cl_backlight_init(&s_backlight, now_ms());
    apply_backlight();
    if (s_lock == NULL || codeloom_wifi_init() != ESP_OK) {
        show_progress(CL_SETUP_STAGE_FAILED, "无法启动 Wi-Fi", "请重启设备");
        vTaskDelete(NULL);
        return;
    }
    make_ap_credentials();

    for (;;) {
        cl_setup_request_t request;
        const char *error;
        bool client_ready = false;
        uint64_t failed_at;

        codeloom_log_heap("before SoftAP");
        if (codeloom_wifi_start_ap(s_ap_ssid, s_ap_password) != ESP_OK ||
            start_server() != ESP_OK) {
            codeloom_wifi_stop();
            show_progress(CL_SETUP_STAGE_FAILED, "热点启动失败", "按 OK 重试");
            while (!pump(1000)) {
            }
            continue;
        }
        codeloom_log_heap("SoftAP + HTTP up");
        show_ap("等待手机连接热点…");

        for (;;) {
            bool ready;
            pump(250);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            ready = s_pending_ready;
            if (ready) {
                request = s_pending;
                memset(&s_pending, 0, sizeof(s_pending));
                s_pending_ready = false;
            }
            xSemaphoreGive(s_lock);
            if (ready) {
                break;
            }
            if (codeloom_wifi_ap_client_ready() != client_ready) {
                client_ready = !client_ready;
                if (client_ready) {
                    codeloom_log_heap("phone joined");
                }
                show_ap(client_ready ? "手机已连接，请打开上方网址"
                                     : "等待手机连接热点…");
            }
        }

        show_progress(CL_SETUP_STAGE_WORKING, "已收到配置", "即将关闭热点…");
        vTaskDelay(pdMS_TO_TICKS(RESPONSE_FLUSH_MS)); // 让浏览器收完响应页
        stop_server();
        codeloom_wifi_stop();

        error = attempt(&request);
        memset(&request, 0, sizeof(request));
        codeloom_wifi_stop();
        ESP_LOGW(TAG, "配网失败: %s", error);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_last_error = error;
        xSemaphoreGive(s_lock);

        show_progress(CL_SETUP_STAGE_FAILED, "设置失败", error);
        failed_at = now_ms();
        while (!pump(250) && now_ms() - failed_at < FAILURE_AUTO_RETURN_MS) {
        }
    }
}
