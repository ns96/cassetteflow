//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_err.h>
#include "pipeline_decode.h"

static const char *TAG = "cf_pipeline_decode";

esp_err_t pipeline_decode_start(void)
{
    return ESP_OK;
}

esp_err_t pipeline_decode_stop(void)
{
    return ESP_OK;
}

void pipeline_decode_status(char *buf, int buf_size)
{
    //TODO
}
