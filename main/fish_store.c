// main/fish_store.c — 见 fish_store.h。每次操作打开并关闭 NVS 句柄，不长期持有。
#include "fish_store.h"

#include <string.h>

#include "esp_log.h"
#include "fish_save.h"
#include "nvs.h"
#include "nvs_flash.h"

#define STORE_NAMESPACE "fishing"
#define STORE_KEY "save"

static const char *TAG = "fish_store";
static bool s_ready;

esp_err_t fish_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s；本次运行不保存进度，未自动擦除分区",
                 esp_err_to_name(err));
    }
    s_ready = err == ESP_OK;
    return err;
}

bool fish_store_load(fg_progress_t *out)
{
    uint8_t data[FISH_SAVE_SIZE];
    size_t length = sizeof(data);
    nvs_handle_t handle;
    esp_err_t err;

    memset(out, 0, sizeof(*out));
    if (!s_ready) {
        return false;
    }
    err = nvs_open(STORE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false; // 第一次运行，命名空间尚未创建
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开存档失败: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_get_blob(handle, STORE_KEY, data, &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (err != ESP_OK || !fish_save_decode(data, length, out)) {
        ESP_LOGW(TAG, "存档无法读取（%s，%u 字节），从新进度开始", esp_err_to_name(err),
                 (unsigned)length);
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

esp_err_t fish_store_save(const fg_progress_t *progress)
{
    uint8_t data[FISH_SAVE_SIZE];
    nvs_handle_t handle;
    esp_err_t err;

    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    fish_save_encode(progress, data);
    err = nvs_open(STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, STORE_KEY, data, sizeof(data));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存进度失败: %s", esp_err_to_name(err));
    }
    return err;
}
