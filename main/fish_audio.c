// main/fish_audio.c — 见 fish_audio.h。
#include "fish_audio.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define AUDIO_TASK_STACK 3072
#define AUDIO_TASK_PRIORITY 6
#define AUDIO_CHUNK 256
// esp_codec_dev 默认音量曲线：0 → -50 dB、100 → 0 dB，每档 0.5 dB。80 ≈ -10 dB。
#define AUDIO_VOLUME 80
#define TAIL_SILENCE_SAMPLES (FISH_SOUND_RATE / 20U) // 50 ms，避免尾音被截断产生爆音

static const char *TAG = "fish_audio";
static QueueHandle_t s_requests;

static bool configure(void)
{
    esp_err_t err = bsp_audio_set_format(FISH_SOUND_RATE, 16, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "音频格式设置失败: %s", esp_err_to_name(err));
        return false;
    }
    bsp_audio_set_volume(AUDIO_VOLUME);
    return true;
}

static void codec_sleep(void)
{
    if (bsp_audio_sleep() != ESP_OK) {
        ESP_LOGW(TAG, "codec 休眠校验失败");
    }
}

static void play(fish_sound_t sound)
{
    int16_t samples[AUDIO_CHUNK];
    size_t total = fish_sound_samples(sound) + TAIL_SILENCE_SAMPLES;

    for (size_t start = 0; start < total; start += AUDIO_CHUNK) {
        size_t count = total - start < AUDIO_CHUNK ? total - start : AUDIO_CHUNK;
        fish_sound_fill(sound, samples, count, start);
        if (bsp_audio_write(samples, count * sizeof(samples[0])) != ESP_OK) {
            ESP_LOGW(TAG, "播放中断");
            return;
        }
    }
}

static void audio_task(void *arg)
{
    bool initialized = false;
    uint8_t request;
    esp_err_t err;

    (void)arg;
    // 开机先完成初始化再休眠：第一次咬钩提示只需唤醒，不必等冷启动。
    err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "音频初始化失败: %s，播放时重试", esp_err_to_name(err));
    } else {
        initialized = true;
        (void)configure();
        codec_sleep();
    }

    for (;;) {
        if (xQueueReceive(s_requests, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!initialized) {
            err = bsp_audio_init();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "音频初始化失败: %s", esp_err_to_name(err));
                continue;
            }
            initialized = true;
        } else if ((err = bsp_audio_wake()) != ESP_OK) {
            ESP_LOGE(TAG, "音频唤醒失败: %s", esp_err_to_name(err));
            continue;
        }
        if (configure()) {
            play((fish_sound_t)request);
        }
        codec_sleep();
    }
}

esp_err_t fish_audio_start(void)
{
    if (s_requests != NULL) {
        return ESP_OK;
    }
    s_requests = xQueueCreate(1, sizeof(uint8_t));
    if (s_requests == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(audio_task, "fish_audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIORITY, NULL) !=
        pdPASS) {
        vQueueDelete(s_requests);
        s_requests = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void fish_audio_play(fish_sound_t sound)
{
    uint8_t request = (uint8_t)sound;

    if (s_requests != NULL && (unsigned)sound < FISH_SOUND_COUNT) {
        xQueueOverwrite(s_requests, &request);
    }
}
