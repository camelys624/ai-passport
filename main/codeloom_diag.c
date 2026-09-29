// main/codeloom_diag.c — 见 codeloom_diag.h。
#include "codeloom_diag.h"

#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "cl_heap";

void codeloom_heap_info(cl_heap_info_t *info)
{
    info->free_bytes = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info->min_free_bytes =
        (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info->largest_block =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void codeloom_log_heap(const char *phase)
{
    cl_heap_info_t info;

    codeloom_heap_info(&info);
    ESP_LOGI(TAG, "heap[%s]: free=%lu min=%lu largest=%lu", phase,
             (unsigned long)info.free_bytes, (unsigned long)info.min_free_bytes,
             (unsigned long)info.largest_block);
}

void codeloom_firmware_version(char *dst, unsigned dst_size)
{
    snprintf(dst, dst_size, "%s", esp_app_get_description()->version);
}
