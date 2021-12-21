//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#include "pipeline_output.h"
#include <esp_log.h>
#include <i2s_stream.h>
#include "bt.h"
#include <string.h>

#define PLAYBACK_RATE       48000

bool output_is_bt = false;
static audio_element_handle_t bt_stream_writer = NULL, i2s_stream_writer = NULL;

static const char *TAG = "cf_pipeline_output";

esp_err_t pipeline_output_init_stream(audio_pipeline_handle_t pipeline, audio_element_handle_t *output_stream_writer,
                                      char *output_stream_name)
{
    ESP_LOGI(TAG, "[-] Create output");

    if (output_is_bt) {
        if (bt_init_output_stream(&bt_stream_writer) == ESP_FAIL) {
            return ESP_FAIL;
        }
        strcpy(output_stream_name, "bt");
        *output_stream_writer = bt_stream_writer;
    } else {
        ESP_LOGI(TAG, "[1] Create i2s_stream_writer");
        i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
        i2s_cfg.type = AUDIO_STREAM_WRITER;
        i2s_cfg.i2s_config.sample_rate = PLAYBACK_RATE;
        i2s_cfg.task_core = 1;
        i2s_stream_writer = i2s_stream_init(&i2s_cfg);
        if (i2s_stream_writer == NULL) {
            ESP_LOGE(TAG, "error init output_stream_writer");
            return ESP_FAIL;
        }
        strcpy(output_stream_name, "i2s");
        *output_stream_writer = i2s_stream_writer;
    }
    return ESP_OK;
}

esp_err_t pipeline_output_set_bt(char *str, size_t str_len)
{
    ESP_LOGE(TAG, "Changing output to BT");

    if (output_is_bt) {
        return ESP_OK;
    }
    output_is_bt = true;
    //TODO switch output in decode mode
    return ESP_OK;
}

esp_err_t pipeline_output_set_sp(void)
{
    ESP_LOGE(TAG, "Changing output to SP");

    if (!output_is_bt) {
        return ESP_OK;
    }
    output_is_bt = false;
    //TODO switch output in decode mode
    return ESP_OK;
}

esp_err_t pipeline_output_periph_start(audio_event_iface_handle_t evt) {
    if (output_is_bt) {
        if (bt_periph_init(evt) != ESP_OK) {
            ESP_LOGE(TAG, "error init BT periph");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}