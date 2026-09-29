// main/codeloom_store.h — 固件侧配置存储：NVS 后端 + codeloom_settings 的封装。
// 调用方必须在 nvs_flash_init() 成功之后使用；函数会阻塞在 Flash 读写上，只在任务中调用。
#pragma once

#include "codeloom_settings.h"
#include "esp_err.h"

// 初始化默认 NVS 分区。失败时不自动擦除（避免误删用户数据），返回错误码。
esp_err_t codeloom_store_init(void);

cl_settings_result_t codeloom_store_load(cl_config_t *out);
cl_settings_result_t codeloom_store_save(const cl_config_t *config);
// 仅清除本应用命名空间 "codeloom"。
cl_settings_result_t codeloom_store_erase(void);
