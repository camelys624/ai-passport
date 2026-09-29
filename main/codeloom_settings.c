// main/codeloom_settings.c — 见 codeloom_settings.h。
#include "codeloom_settings.h"

#include <string.h>

#include "codeloom_protocol.h"
#include "codeloom_setup_form.h"
#include "codeloom_text.h"
#include "codeloom_url.h"

#define KEY_VERSION "ver"
#define KEY_SSID "ssid"
#define KEY_PASSWORD "pass"
#define KEY_URL "url"
#define KEY_TOKEN "token"
#define KEY_DEVICE_ID "dev_id"
#define KEY_WORKSPACE "ws_name"

bool cl_config_valid(const cl_config_t *config)
{
    cl_url_t url;

    if (config == NULL || !cl_wifi_ssid_valid(config->ssid) ||
        !cl_wifi_password_valid(config->password) || !cl_url_parse(config->server_url, &url) ||
        strcmp(url.normalized, config->server_url) != 0 ||
        !cl_device_token_valid(config->device_token) || !cl_id_is_safe(config->device_id) ||
        strncmp(config->device_id, "dev_", 4) != 0) {
        return false;
    }
    return memchr(config->workspace_name, '\0', sizeof(config->workspace_name)) != NULL &&
           cl_utf8_valid(config->workspace_name, strlen(config->workspace_name));
}

static cl_settings_result_t load_str(const cl_settings_backend_t *backend, void *ctx,
                                     const char *key, char *value, size_t size)
{
    size_t length = size;
    cl_backend_status_t status = backend->get_str(ctx, key, value, &length);

    if (status == CL_BACKEND_NOT_FOUND) {
        return CL_SETTINGS_NOT_FOUND;
    }
    if (status != CL_BACKEND_OK) {
        return CL_SETTINGS_IO_ERROR;
    }
    if (length == 0 || length > size || value[length - 1] != '\0') {
        return CL_SETTINGS_NOT_FOUND;
    }
    return CL_SETTINGS_OK;
}

cl_settings_result_t cl_settings_load(const cl_settings_backend_t *backend, void *ctx,
                                      cl_config_t *out)
{
    const struct {
        const char *key;
        char *value;
        size_t size;
    } fields[] = {
        {KEY_SSID, out->ssid, sizeof(out->ssid)},
        {KEY_PASSWORD, out->password, sizeof(out->password)},
        {KEY_URL, out->server_url, sizeof(out->server_url)},
        {KEY_TOKEN, out->device_token, sizeof(out->device_token)},
        {KEY_DEVICE_ID, out->device_id, sizeof(out->device_id)},
        {KEY_WORKSPACE, out->workspace_name, sizeof(out->workspace_name)},
    };
    uint8_t version = 0;
    cl_backend_status_t status;

    if (backend == NULL || out == NULL) {
        return CL_SETTINGS_IO_ERROR;
    }
    memset(out, 0, sizeof(*out));
    status = backend->get_u8(ctx, KEY_VERSION, &version);
    if (status == CL_BACKEND_NOT_FOUND) {
        return CL_SETTINGS_NOT_FOUND;
    }
    if (status != CL_BACKEND_OK) {
        return CL_SETTINGS_IO_ERROR;
    }
    if (version != CL_SETTINGS_VERSION) {
        return CL_SETTINGS_NOT_FOUND;
    }
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        cl_settings_result_t result = load_str(backend, ctx, fields[i].key, fields[i].value,
                                               fields[i].size);
        if (result != CL_SETTINGS_OK) {
            memset(out, 0, sizeof(*out));
            return result;
        }
    }
    if (!cl_config_valid(out)) {
        memset(out, 0, sizeof(*out));
        return CL_SETTINGS_NOT_FOUND;
    }
    return CL_SETTINGS_OK;
}

cl_settings_result_t cl_settings_save(const cl_settings_backend_t *backend, void *ctx,
                                      const cl_config_t *config)
{
    if (backend == NULL) {
        return CL_SETTINGS_IO_ERROR;
    }
    if (!cl_config_valid(config)) {
        return CL_SETTINGS_INVALID;
    }
    // 先清空并提交：写入中途掉电时，缺少版本号的数据会被当作“无配置”。
    if (backend->erase_all(ctx) != CL_BACKEND_OK || backend->commit(ctx) != CL_BACKEND_OK ||
        backend->set_str(ctx, KEY_SSID, config->ssid) != CL_BACKEND_OK ||
        backend->set_str(ctx, KEY_PASSWORD, config->password) != CL_BACKEND_OK ||
        backend->set_str(ctx, KEY_URL, config->server_url) != CL_BACKEND_OK ||
        backend->set_str(ctx, KEY_TOKEN, config->device_token) != CL_BACKEND_OK ||
        backend->set_str(ctx, KEY_DEVICE_ID, config->device_id) != CL_BACKEND_OK ||
        backend->set_str(ctx, KEY_WORKSPACE, config->workspace_name) != CL_BACKEND_OK ||
        backend->commit(ctx) != CL_BACKEND_OK ||
        backend->set_u8(ctx, KEY_VERSION, CL_SETTINGS_VERSION) != CL_BACKEND_OK ||
        backend->commit(ctx) != CL_BACKEND_OK) {
        return CL_SETTINGS_IO_ERROR;
    }
    return CL_SETTINGS_OK;
}

cl_settings_result_t cl_settings_erase(const cl_settings_backend_t *backend, void *ctx)
{
    if (backend == NULL || backend->erase_all(ctx) != CL_BACKEND_OK ||
        backend->commit(ctx) != CL_BACKEND_OK) {
        return CL_SETTINGS_IO_ERROR;
    }
    return CL_SETTINGS_OK;
}
