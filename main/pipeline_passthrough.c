//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#include <audio_pipeline.h>
#include <esp_log.h>
#include <i2s_stream.h>
#include <equalizer.h>

#include "pipeline_passthrough.h"
#include "pipeline.h"

#define USE_EQ      (1)

extern int equalizer_band_gain[20];

static const char *TAG = "cf_pipeline_passthrough";

static audio_pipeline_handle_t pipeline = NULL;
static audio_element_handle_t i2s_stream_writer = NULL, i2s_stream_reader = NULL, equalizer = NULL;

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
    i2s_cfg.task_core = 1;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    if (i2s_stream_writer == NULL) {
        ESP_LOGE(TAG, "error init i2s_stream_writer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[3] Create i2s stream to read data from codec chip");
    i2s_stream_cfg_t i2s_cfg_read = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg_read.type = AUDIO_STREAM_READER;
    i2s_cfg_read.task_core = 1;
    i2s_stream_reader = i2s_stream_init(&i2s_cfg_read);
    if (i2s_stream_reader == NULL) {
        ESP_LOGE(TAG, "error init i2s_stream_reader");
        return ESP_FAIL;
    }

#ifdef USE_EQ
    ESP_LOGI(TAG, "[4] Create equalizer");
    equalizer_cfg_t eq_cfg = DEFAULT_EQUALIZER_CONFIG();
    eq_cfg.samplerate = 44100;  // default rate of i2s streams
    eq_cfg.channel = 2;
    eq_cfg.set_gain = equalizer_band_gain;
    eq_cfg.task_core = 1;
    eq_cfg.task_prio = 10;
    equalizer = equalizer_init(&eq_cfg);
    if (equalizer == NULL) {
        ESP_LOGE(TAG, "error init equalizer");
        return ESP_FAIL;
    }
#endif

    ESP_LOGI(TAG, "[4] Register all elements to audio pipeline");
    ESP_ERROR_CHECK(audio_pipeline_register(pipeline, i2s_stream_reader, "i2s_read"));
    ESP_ERROR_CHECK(audio_pipeline_register(pipeline, i2s_stream_writer, "i2s_write"));
    ESP_ERROR_CHECK(audio_pipeline_register(pipeline, equalizer, "equalizer"));

    ESP_LOGI(TAG, "[5] Link it together [codec_chip]-->i2s_stream_reader-->equalizer-->i2s_stream_writer-->[codec_chip]");
    const char *link_tag[3] = {"i2s_read", "equalizer", "i2s_write"};
    ESP_ERROR_CHECK(audio_pipeline_link(pipeline, link_tag, 3));

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

    return ESP_OK;
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


/**
 * 10 bands, channels are equal
 * @return ESP_OK or error
 */
esp_err_t pipeline_passthrough_set_equalizer(int band_gain[10])
{
    esp_err_t ret = ESP_OK;

    for (int i = 0; i < 10; ++i) {
        if (equalizer != NULL) {
            ret = equalizer_set_gain_info(equalizer, i, band_gain[i], true);
            if (ret != ESP_OK) {
                break;
            }
        }
    }

    return ret;
}
