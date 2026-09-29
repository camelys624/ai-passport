// main/codeloom_events.c — 见 codeloom_events.h。
#include "codeloom_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define EVENT_QUEUE_DEPTH 12

static QueueHandle_t s_queue;

esp_err_t codeloom_events_init(void)
{
    if (s_queue == NULL) {
        s_queue = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(cl_event_t));
    }
    return s_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

bool codeloom_events_post(const cl_event_t *event, uint32_t wait_ms)
{
    return s_queue != NULL && xQueueSend(s_queue, event, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

bool codeloom_events_receive(cl_event_t *event, uint32_t wait_ms)
{
    return s_queue != NULL && xQueueReceive(s_queue, event, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}
