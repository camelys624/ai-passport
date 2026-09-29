// main/codeloom_wifi.h — Wi-Fi 生命周期（SoftAP 配网 / STA 连接与指数退避重连）。
//
// 全部 API 只在应用任务或工作任务中调用（会阻塞在驱动调用上），不可在按键回调中调用。
// 驱动使用 WIFI_STORAGE_RAM：凭据只由 codeloom_store 保存，驱动不会另存一份。
// 任何日志都不打印 Wi-Fi 口令。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    CL_WIFI_RESULT_CONNECTED = 0,
    CL_WIFI_RESULT_AUTH_FAILED,
    CL_WIFI_RESULT_NO_AP,
    CL_WIFI_RESULT_TIMEOUT,
    CL_WIFI_RESULT_ERROR,
} cl_wifi_result_t;

typedef struct {
    bool connected;
    char ip[16];
    int8_t rssi;
    uint8_t last_reason; // 最近一次断开原因（wifi_err_reason_t），0 = 无
} codeloom_wifi_info_t;

// 初始化 esp_netif、默认事件循环和 Wi-Fi 驱动。幂等。
esp_err_t codeloom_wifi_init(void);

// 启动 STA 并连接。persistent=true：断线后按 cl_wifi_retry_delay_ms 无限重连；
// persistent=false（配网验证）：最多尝试 4 次，认证失败 2 次即判定失败。
esp_err_t codeloom_wifi_start_sta(const char *ssid, const char *password, bool persistent);

// 等待 STA 获得 IP 或判定失败（仅对 persistent=false 有意义）。
cl_wifi_result_t codeloom_wifi_wait_sta(uint32_t timeout_ms);

// 等待 STA 获得 IP；超时返回 false。供同步任务阻塞使用。
bool codeloom_wifi_wait_connected(uint32_t timeout_ms);

// 启动 WPA2 SoftAP（信道 1，最多 1 个客户端，DHCP 由 esp_netif 管理，地址 192.168.4.1）。
esp_err_t codeloom_wifi_start_ap(const char *ssid, const char *password);

// 手机是否已通过 DHCP 获得地址（IP_EVENT_AP_STAIPASSIGNED 之后、断开之前）。
bool codeloom_wifi_ap_client_ready(void);

// 停止驱动（AP 或 STA），取消待执行的重连。
esp_err_t codeloom_wifi_stop(void);

// 用射频熵源生成随机数：若驱动未启动，会临时以 STA 模式启动再停止。
esp_err_t codeloom_wifi_random(void *buffer, size_t length);

// 读取 AP 模式 MAC 的末两字节，用于 "Codeloom-XXXX" / "Passport-XXXX"。
void codeloom_wifi_suffix(char out[5]);

void codeloom_wifi_get_info(codeloom_wifi_info_t *info);

// 清除驱动自身在 NVS 中可能保存的 Wi-Fi 配置（重置配置时调用）。
esp_err_t codeloom_wifi_forget(void);
