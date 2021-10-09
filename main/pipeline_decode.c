//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_err.h>
#include <audio_event_iface.h>
#include <esp_log.h>
#include "pipeline_decode.h"

static const char *TAG = "cf_pipeline_decode";

esp_err_t pipeline_decode_start(void)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    return ESP_OK;
}

esp_err_t pipeline_decode_maybe_handle_event(audio_event_iface_handle_t evt, audio_event_iface_msg_t *msg)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    return ESP_OK;
}

esp_err_t pipeline_decode_stop(void)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    return ESP_OK;
}

void pipeline_decode_status(char *buf, int buf_size)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    //TODO
}
