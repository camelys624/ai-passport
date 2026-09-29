// main/codeloom_store.c — 见 codeloom_store.h。每次操作打开并关闭 NVS 句柄，不长期持有。
#include "codeloom_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define STORE_NAMESPACE "codeloom"

static const char *TAG = "cl_store";

static cl_backend_status_t map_err(esp_err_t err)
{
    if (err == ESP_OK) {
        return CL_BACKEND_OK;
    }
    return err == ESP_ERR_NVS_NOT_FOUND ? CL_BACKEND_NOT_FOUND : CL_BACKEND_ERROR;
}

static cl_backend_status_t nvs_get_str_cb(void *ctx, const char *key, char *value, size_t *length)
{
    return map_err(nvs_get_str(*(nvs_handle_t *)ctx, key, value, length));
}

static cl_backend_status_t nvs_set_str_cb(void *ctx, const char *key, const char *value)
{
    return map_err(nvs_set_str(*(nvs_handle_t *)ctx, key, value));
}

static cl_backend_status_t nvs_get_u8_cb(void *ctx, const char *key, uint8_t *value)
{
    return map_err(nvs_get_u8(*(nvs_handle_t *)ctx, key, value));
}

static cl_backend_status_t nvs_set_u8_cb(void *ctx, const char *key, uint8_t value)
{
    return map_err(nvs_set_u8(*(nvs_handle_t *)ctx, key, value));
}

static cl_backend_status_t nvs_erase_all_cb(void *ctx)
{
    return map_err(nvs_erase_all(*(nvs_handle_t *)ctx));
}

static cl_backend_status_t nvs_commit_cb(void *ctx)
{
    return map_err(nvs_commit(*(nvs_handle_t *)ctx));
}

static const cl_settings_backend_t NVS_BACKEND = {
    .get_str = nvs_get_str_cb,
    .set_str = nvs_set_str_cb,
    .get_u8 = nvs_get_u8_cb,
    .set_u8 = nvs_set_u8_cb,
    .erase_all = nvs_erase_all_cb,
    .commit = nvs_commit_cb,
};

esp_err_t codeloom_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s；未自动擦除分区", esp_err_to_name(err));
    }
    return err;
}

cl_settings_result_t codeloom_store_load(cl_config_t *out)
{
    nvs_handle_t handle;
    cl_settings_result_t result;
    esp_err_t err = nvs_open(STORE_NAMESPACE, NVS_READONLY, &handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return CL_SETTINGS_NOT_FOUND; // 命名空间尚未创建
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开配置失败: %s", esp_err_to_name(err));
        return CL_SETTINGS_IO_ERROR;
    }
    result = cl_settings_load(&NVS_BACKEND, &handle, out);
    nvs_close(handle);
    return result;
}

static cl_settings_result_t with_rw_handle(const cl_config_t *config, bool erase)
{
    nvs_handle_t handle;
    cl_settings_result_t result;
    esp_err_t err = nvs_open(STORE_NAMESPACE, NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开配置失败: %s", esp_err_to_name(err));
        return CL_SETTINGS_IO_ERROR;
    }
    result = erase ? cl_settings_erase(&NVS_BACKEND, &handle)
                   : cl_settings_save(&NVS_BACKEND, &handle, config);
    nvs_close(handle);
    return result;
}

cl_settings_result_t codeloom_store_save(const cl_config_t *config)
{
    return with_rw_handle(config, false);
}

cl_settings_result_t codeloom_store_erase(void)
{
    return with_rw_handle(NULL, true);
}
