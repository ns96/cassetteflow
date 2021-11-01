//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#include <audio_pipeline.h>
#include <esp_log.h>
#include <i2s_stream.h>
#include "pipeline_passthrough.h"
#include "pipeline.h"

static const char *TAG = "cf_pipeline_passthrough";

static audio_pipeline_handle_t pipeline;
static audio_element_handle_t i2s_stream_writer, i2s_stream_reader;

esp_err_t pipeline_passthrough_start(audio_event_iface_handle_t evt)
{
    ESP_LOGI(TAG, "[1] start");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (pipeline == NULL) {
        ESP_LOGE(TAG, "error audio_pipeline_init");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    if (i2s_stream_writer == NULL) {
        ESP_LOGE(TAG, "error init i2s_stream_writer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[3] Create i2s stream to read data from codec chip");
    i2s_stream_cfg_t i2s_cfg_read = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg_read.type = AUDIO_STREAM_READER;
    i2s_stream_reader = i2s_stream_init(&i2s_cfg_read);
    if (i2s_stream_reader == NULL) {
        ESP_LOGE(TAG, "error init i2s_stream_reader");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[4] Register all elements to audio pipeline");
    ESP_ERROR_CHECK(audio_pipeline_register(pipeline, i2s_stream_reader, "i2s_read"));
    ESP_ERROR_CHECK(audio_pipeline_register(pipeline, i2s_stream_writer, "i2s_write"));

    ESP_LOGI(TAG, "[5] Link it together [codec_chip]-->i2s_stream_reader-->i2s_stream_writer-->[codec_chip]");
    const char *link_tag[2] = {"i2s_read", "i2s_write"};
    ESP_ERROR_CHECK(audio_pipeline_link(pipeline, link_tag, 2));

    ESP_LOGI(TAG, "[6] Listening event from all elements of pipeline");
    ESP_ERROR_CHECK(audio_pipeline_set_listener(pipeline, evt));

    ESP_LOGI(TAG, "[7] Start audio_pipeline");
    ESP_ERROR_CHECK(audio_pipeline_run(pipeline));

    ESP_ERROR_CHECK(esp_event_post_to(pipeline_event_loop, PIPELINE_EVENTS,
                                      PIPELINE_PASSTHROUGH_STARTED, NULL, 0, portMAX_DELAY));

    return ESP_OK;
}

esp_err_t pipeline_passthrough_event_loop(audio_event_iface_handle_t evt)
{
    ESP_LOGI(TAG, "event_loop");
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
        }
    }

    return pipeline_passthrough_stop();
}

esp_err_t pipeline_passthrough_stop(void)
{
    ESP_LOGI(TAG, "stop");

    if (pipeline != NULL) {
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        audio_pipeline_deinit(pipeline);
        pipeline = NULL;
    }

    return ESP_OK;
}
