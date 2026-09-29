// main/codeloom_diag.h — 内部 RAM 诊断：空闲堆、历史最低值和最大连续块。
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t free_bytes;
    uint32_t min_free_bytes;
    uint32_t largest_block;
} cl_heap_info_t;

void codeloom_heap_info(cl_heap_info_t *info);

// 打印一行 "heap[phase]: free=… min=… largest=…"。
void codeloom_log_heap(const char *phase);

// 应用版本（esp_app_desc_t.version，即 git describe），截断到 dst_size-1 字节。
void codeloom_firmware_version(char *dst, unsigned dst_size);
