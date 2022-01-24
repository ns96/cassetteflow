//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_RAW_QUEUE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_RAW_QUEUE_H

#include <esp_err.h>

// contains 1 line decoded from tape
typedef struct
{
    char line[64];
} raw_queue_message_t;

esp_err_t raw_queue_init(int index);
void raw_queue_reset(int index);
esp_err_t raw_queue_send(int index, raw_queue_message_t *msg);
esp_err_t raw_queue_get(int index, raw_queue_message_t *msg, int timeout_ticks);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_RAW_QUEUE_H
