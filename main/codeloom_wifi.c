// main/codeloom_wifi.c — 见 codeloom_wifi.h。
//
// 事件处理器运行在默认事件循环任务中，只更新状态位、记录原因并安排 esp_timer 重连，
// 不做阻塞操作。重连定时器回调运行在 esp_timer 任务中，只调用 esp_wifi_connect()。
#include "codeloom_wifi.h"

#include <stdio.h>
#include <string.h>

#include "codeloom_timing.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BIT_CONNECTED BIT0
#define BIT_FAILED BIT1

#define PROVISION_MAX_ATTEMPTS 4U
#define PROVISION_MAX_AUTH_FAILURES 2U
#define PROVISION_RETRY_MS 1000U

static const char *TAG = "cl_wifi";

static bool s_initialized;
static bool s_started;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static EventGroupHandle_t s_events;
static esp_timer_handle_t s_retry_timer;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// 以下状态由事件任务写、其他任务读；用 s_lock 保护多字段一致性。
static volatile bool s_sta_wanted;
static volatile bool s_persistent;
static unsigned s_attempts;
static unsigned s_auth_failures;
static uint8_t s_last_reason;
static cl_wifi_result_t s_fail_result;
static esp_ip4_addr_t s_ip;
static volatile bool s_ap_client_ready;

static bool reason_is_auth(uint8_t reason)
{
    return reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_MIC_FAILURE ||
           reason == WIFI_REASON_AUTH_EXPIRE;
}

static bool reason_is_no_ap(uint8_t reason)
{
    return reason == WIFI_REASON_NO_AP_FOUND ||
           reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
           reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD ||
           reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD;
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    if (s_sta_wanted) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
        }
    }
}

static void schedule_retry(uint32_t delay_ms)
{
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000U);
}

static void on_sta_disconnected(const wifi_event_sta_disconnected_t *event)
{
    uint8_t reason = event->reason;
    uint32_t delay;

    xEventGroupClearBits(s_events, BIT_CONNECTED);
    portENTER_CRITICAL(&s_lock);
    s_last_reason = reason;
    s_ip.addr = 0;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "STA 断开，原因 %u", reason);
    if (!s_sta_wanted) {
        return;
    }
    if (s_persistent) {
        delay = cl_wifi_retry_delay_ms(s_attempts);
        if (s_attempts < 32U) {
            s_attempts++;
        }
        schedule_retry(delay);
        return;
    }
    s_attempts++;
    if (reason_is_auth(reason)) {
        s_auth_failures++;
    }
    if (s_auth_failures >= PROVISION_MAX_AUTH_FAILURES || s_attempts >= PROVISION_MAX_ATTEMPTS) {
        s_fail_result = s_auth_failures >= PROVISION_MAX_AUTH_FAILURES ? CL_WIFI_RESULT_AUTH_FAILED
                        : reason_is_no_ap(reason)                       ? CL_WIFI_RESULT_NO_AP
                                                                         : CL_WIFI_RESULT_TIMEOUT;
        s_sta_wanted = false;
        xEventGroupSetBits(s_events, BIT_FAILED);
        return;
    }
    schedule_retry(PROVISION_RETRY_MS);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_sta_wanted) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            on_sta_disconnected((const wifi_event_sta_disconnected_t *)data);
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "手机已加入热点");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "手机已离开热点");
            s_ap_client_ready = false;
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
            portENTER_CRITICAL(&s_lock);
            s_ip = event->ip_info.ip;
            s_last_reason = 0;
            portEXIT_CRITICAL(&s_lock);
            s_attempts = 0;
            ESP_LOGI(TAG, "STA 获得 IP " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(s_events, BIT_CONNECTED);
        } else if (id == IP_EVENT_STA_LOST_IP) {
            xEventGroupClearBits(s_events, BIT_CONNECTED);
        } else if (id == IP_EVENT_AP_STAIPASSIGNED) {
            const ip_event_ap_staipassigned_t *event = (const ip_event_ap_staipassigned_t *)data;
            ESP_LOGI(TAG, "DHCP 已分配 " IPSTR, IP2STR(&event->ip));
            s_ap_client_ready = true;
        }
    }
}

esp_err_t codeloom_wifi_init(void)
{
    esp_err_t err;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    const esp_timer_create_args_t timer_args = {
        .callback = retry_timer_cb,
        .name = "cl_wifi_retry",
    };

    if (s_initialized) {
        return ESP_OK;
    }
    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (s_events == NULL) {
        s_events = xEventGroupCreate();
        if (s_events == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_retry_timer == NULL) {
        err = esp_timer_create(&timer_args, &s_retry_timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler,
                                                  NULL, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, event_handler,
                                                  NULL, NULL);
    }
    if (err != ESP_OK) {
        esp_wifi_deinit();
        return err;
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t codeloom_wifi_stop(void)
{
    s_sta_wanted = false;
    if (s_retry_timer != NULL) {
        esp_timer_stop(s_retry_timer);
    }
    if (!s_started) {
        return ESP_OK;
    }
    s_started = false;
    s_ap_client_ready = false;
    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAILED);
    return esp_wifi_stop();
}

esp_err_t codeloom_wifi_start_sta(const char *ssid, const char *password, bool persistent)
{
    wifi_config_t config = {0};
    esp_err_t err;

    if (ssid == NULL || password == NULL || strlen(ssid) > sizeof(config.sta.ssid) ||
        strlen(password) >= sizeof(config.sta.password)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = codeloom_wifi_init();
    if (err != ESP_OK) {
        return err;
    }
    codeloom_wifi_stop();
    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAILED); // 不沿用上一次尝试的结果
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    // 允许开放网络；有口令时要求至少 WPA2，避免降级到 WEP/WPA。
    config.sta.threshold.authmode = password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    s_attempts = 0;
    s_auth_failures = 0;
    s_fail_result = CL_WIFI_RESULT_TIMEOUT;
    s_persistent = persistent;
    s_sta_wanted = true;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    memset(&config, 0, sizeof(config));
    if (err != ESP_OK) {
        s_sta_wanted = false;
        ESP_LOGE(TAG, "STA 启动失败: %s", esp_err_to_name(err));
        return err;
    }
    s_started = true;
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    ESP_LOGI(TAG, "STA 启动，连接到已配置网络");
    return ESP_OK;
}

cl_wifi_result_t codeloom_wifi_wait_sta(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED | BIT_FAILED, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_CONNECTED) {
        return CL_WIFI_RESULT_CONNECTED;
    }
    if (bits & BIT_FAILED) {
        return s_fail_result;
    }
    s_sta_wanted = false;
    esp_timer_stop(s_retry_timer);
    return reason_is_no_ap(s_last_reason) ? CL_WIFI_RESULT_NO_AP : CL_WIFI_RESULT_TIMEOUT;
}

bool codeloom_wifi_wait_connected(uint32_t timeout_ms)
{
    if (s_events == NULL) {
        return false;
    }
    return (xEventGroupWaitBits(s_events, BIT_CONNECTED, pdFALSE, pdFALSE,
                                pdMS_TO_TICKS(timeout_ms)) &
            BIT_CONNECTED) != 0;
}

esp_err_t codeloom_wifi_start_ap(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    esp_err_t err;
    size_t ssid_len = ssid != NULL ? strlen(ssid) : 0;

    if (ssid_len == 0 || ssid_len > sizeof(config.ap.ssid) || password == NULL ||
        strlen(password) < 8 || strlen(password) >= sizeof(config.ap.password)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = codeloom_wifi_init();
    if (err != ESP_OK) {
        return err;
    }
    codeloom_wifi_stop();
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    memcpy(config.ap.ssid, ssid, ssid_len);
    config.ap.ssid_len = (uint8_t)ssid_len;
    memcpy(config.ap.password, password, strlen(password));
    config.ap.channel = 1;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = 1; // 单用户配网；见 phoenixzhc SoftAP 预算经验
    config.ap.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    memset(&config, 0, sizeof(config));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SoftAP 启动失败: %s", esp_err_to_name(err));
        return err;
    }
    s_started = true;
    s_ap_client_ready = false;
    ESP_LOGI(TAG, "SoftAP %s 已启动", ssid);
    return ESP_OK;
}

bool codeloom_wifi_ap_client_ready(void)
{
    return s_ap_client_ready;
}

esp_err_t codeloom_wifi_random(void *buffer, size_t length)
{
    esp_err_t err = codeloom_wifi_init();
    bool temporary = !s_started;

    if (err != ESP_OK) {
        return err;
    }
    // esp_fill_random 仅在射频开启时是真随机数；配网口令必须不可预测。
    if (temporary) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) {
            err = esp_wifi_start();
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    esp_fill_random(buffer, length);
    if (temporary) {
        esp_wifi_stop();
    }
    return ESP_OK;
}

void codeloom_wifi_suffix(char out[5])
{
    uint8_t mac[6] = {0};

    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, 5, "%02X%02X", mac[4], mac[5]);
}

void codeloom_wifi_get_info(codeloom_wifi_info_t *info)
{
    wifi_ap_record_t ap;
    esp_ip4_addr_t ip;

    memset(info, 0, sizeof(*info));
    portENTER_CRITICAL(&s_lock);
    ip = s_ip;
    info->last_reason = s_last_reason;
    portEXIT_CRITICAL(&s_lock);
    info->connected = s_events != NULL && (xEventGroupGetBits(s_events) & BIT_CONNECTED) != 0;
    if (info->connected) {
        snprintf(info->ip, sizeof(info->ip), IPSTR, IP2STR(&ip));
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            info->rssi = ap.rssi;
        }
    }
}

esp_err_t codeloom_wifi_forget(void)
{
    esp_err_t err = codeloom_wifi_init();
    return err == ESP_OK ? esp_wifi_restore() : err;
}
