// main/codeloom_audio.c — 见 codeloom_audio.h。
#include "codeloom_audio.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp_audio.h"
#include "codeloom_chime.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define AUDIO_TASK_STACK 3072
#define AUDIO_CHUNK 256
#define AUDIO_VOLUME 70
#define TAIL_SILENCE_SAMPLES (CL_CHIME_SAMPLE_RATE / 20) // 50 ms，避免尾音被截断产生爆音

static const char *TAG = "cl_audio";
static QueueHandle_t s_requests;

static bool prepare_codec(bool *initialized)
{
    esp_err_t err;

    if (!*initialized) {
        err = bsp_audio_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "音频初始化失败: %s", esp_err_to_name(err));
            return false;
        }
        *initialized = true;
    } else {
        err = bsp_audio_wake();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "音频唤醒失败: %s", esp_err_to_name(err));
            return false;
        }
    }
    err = bsp_audio_set_format(CL_CHIME_SAMPLE_RATE, 16, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "音频格式设置失败: %s", esp_err_to_name(err));
        return false;
    }
    bsp_audio_set_volume(AUDIO_VOLUME);
    return true;
}

static void audio_task(void *arg)
{
    int16_t samples[AUDIO_CHUNK];
    bool initialized = false;
    uint8_t request;

    (void)arg;
    for (;;) {
        size_t total;

        if (xQueueReceive(s_requests, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!prepare_codec(&initialized)) {
            continue;
        }
        total = cl_chime_total_samples(CL_CHIME_SAMPLE_RATE) + TAIL_SILENCE_SAMPLES;
        for (size_t start = 0; start < total; start += AUDIO_CHUNK) {
            size_t count = total - start < AUDIO_CHUNK ? total - start : AUDIO_CHUNK;
            cl_chime_fill(samples, count, start, CL_CHIME_SAMPLE_RATE);
            if (bsp_audio_write(samples, count * sizeof(samples[0])) != ESP_OK) {
                break;
            }
        }
        if (bsp_audio_sleep() != ESP_OK) {
            ESP_LOGW(TAG, "codec 休眠校验失败");
        }
    }
}

esp_err_t codeloom_audio_start(void)
{
    if (s_requests != NULL) {
        return ESP_OK;
    }
    s_requests = xQueueCreate(1, sizeof(uint8_t));
    if (s_requests == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(audio_task, "cl_audio", AUDIO_TASK_STACK, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(s_requests);
        s_requests = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void codeloom_audio_chime(void)
{
    uint8_t request = 1;

    if (s_requests != NULL) {
        xQueueOverwrite(s_requests, &request);
    }
}
