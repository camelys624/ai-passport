// main/codeloom_settings.h — 已配对配置的校验与版本化持久化（纯 C，存储后端可替换）。
//
// 固件使用 codeloom_settings_nvs.c 提供的 NVS 后端（命名空间 "codeloom"）；主机测试注入
// 内存后端。写入顺序保证掉电安全：先擦除命名空间并提交，再写全部字段，最后写入版本号并提交。
// 读取时版本号不匹配或任一字段校验失败，都视为“无配置”，设备回到配网模式。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "codeloom_types.h"

#define CL_SETTINGS_VERSION 1

typedef enum {
    CL_BACKEND_OK = 0,
    CL_BACKEND_NOT_FOUND,
    CL_BACKEND_ERROR,
} cl_backend_status_t;

typedef struct {
    // *length 输入为缓冲容量（含 NUL），输出为实际长度（含 NUL）。放不下返回 ERROR。
    cl_backend_status_t (*get_str)(void *ctx, const char *key, char *value, size_t *length);
    cl_backend_status_t (*set_str)(void *ctx, const char *key, const char *value);
    cl_backend_status_t (*get_u8)(void *ctx, const char *key, uint8_t *value);
    cl_backend_status_t (*set_u8)(void *ctx, const char *key, uint8_t value);
    cl_backend_status_t (*erase_all)(void *ctx);
    cl_backend_status_t (*commit)(void *ctx);
} cl_settings_backend_t;

typedef enum {
    CL_SETTINGS_OK = 0,
    CL_SETTINGS_NOT_FOUND, // 未配置、版本不符或数据无效
    CL_SETTINGS_INVALID,   // 待保存的配置未通过校验
    CL_SETTINGS_IO_ERROR,
} cl_settings_result_t;

// 校验完整配置：SSID/口令规则、服务器地址已规范化、令牌 "awd_"、设备 ID "dev_"、
// 工作区名称为合法 UTF-8。
bool cl_config_valid(const cl_config_t *config);

cl_settings_result_t cl_settings_load(const cl_settings_backend_t *backend, void *ctx,
                                      cl_config_t *out);
cl_settings_result_t cl_settings_save(const cl_settings_backend_t *backend, void *ctx,
                                      const cl_config_t *config);
cl_settings_result_t cl_settings_erase(const cl_settings_backend_t *backend, void *ctx);
