//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "raw_queue.h"

static QueueHandle_t queue[2] = {NULL, NULL};

esp_err_t raw_queue_init(int index)
{
    queue[index] = xQueueCreate(5, sizeof(raw_queue_message_t));

    return queue[index] != 0 ? ESP_OK : ESP_FAIL;
}

/**
 * Reset queue to empty state
 * @return
 */
void raw_queue_reset(int index)
{
    xQueueReset(queue[index]);
}

esp_err_t raw_queue_send(int index, raw_queue_message_t *msg)
{
    return xQueueSendToBack(queue[index], msg, 0) == pdPASS ? ESP_OK : ESP_FAIL;
}

/**
 * Blocking waiting for a message to receive from the queue
 * @param msg
 * @param timeout_ticks
 * @return ESP_OK is message read
 */
esp_err_t raw_queue_get(int index, raw_queue_message_t *msg, int timeout_ticks)
{
    return xQueueReceive(queue[index], msg, timeout_ticks) == pdTRUE ? ESP_OK : ESP_FAIL;
}
