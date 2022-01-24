//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#include "pipeline_output.h"
#include <esp_log.h>
#include <i2s_stream.h>
#include "bt.h"
#include <string.h>
#include "pipeline_playback.h"
#include "a2dp_stream.h"
#include "esp_a2dp_api.h"
#include "pipeline.h"

#define PLAYBACK_RATE       48000

bool output_is_bt = false;
static audio_element_handle_t i2s_stream_writer = NULL;
static audio_element_handle_t bt_stream_writer = NULL;

extern audio_event_iface_handle_t evt;

static audio_element_handle_t *output_stream = NULL;

static const char *sp_output_stream_name = {"i2s"};
static const char *bt_output_stream_name = {"bt"};
static char output_stream_name[4] = {0};

static const char *TAG = "cf_pipeline_output";

static void bt_a2d_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                ESP_LOGI(TAG, "My a2dp source connected");
                pipeline_unpause();
            }
            break;
        default:
            ESP_LOGI(TAG, "My A2D event %d", event);
            break;
    }
}

esp_err_t pipeline_output_init_stream(audio_element_handle_t **output_stream_writer,
                                      char *stream_name)
{
    ESP_LOGI(TAG, "[-] Create output");
    audio_element_state_t state = 0;

    if (output_is_bt) {
        if (bt_stream_writer == NULL) {
            ESP_LOGI(TAG, "[] Create Bluetooth stream");

            a2dp_stream_config_t a2dp_config = {
                .type = AUDIO_STREAM_WRITER,
                .user_callback = { .user_a2d_cb = bt_a2d_callback,
                    .user_a2d_sink_data_cb = NULL,
                    .user_a2d_source_data_cb = NULL},
            };

            bt_init();
            bt_stream_writer = a2dp_stream_init(&a2dp_config);
            bt_connect_device();
            pipeline_pause();
        }
        strcpy(stream_name, bt_output_stream_name);
        strcpy(output_stream_name, bt_output_stream_name);
        output_stream = &bt_stream_writer;
    } else {
        ESP_LOGI(TAG, "[1] Create i2s_stream_writer");
        i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
        i2s_cfg.type = AUDIO_STREAM_WRITER;
        i2s_cfg.i2s_config.sample_rate = PLAYBACK_RATE;
        i2s_cfg.task_core = 1;
        i2s_stream_writer = i2s_stream_init(&i2s_cfg);

        strcpy(stream_name, sp_output_stream_name);
        strcpy(output_stream_name, sp_output_stream_name);
        output_stream = &i2s_stream_writer;
    }

    if (output_stream == NULL) {
        ESP_LOGE(TAG, "Error init %s stream", output_stream_name);
        return ESP_FAIL;
    }
    state = audio_element_get_state(*output_stream);
    ESP_LOGI(TAG, "[1] Created output stream state %d", state);

    *output_stream_writer = output_stream;
    return ESP_OK;
}

esp_err_t pipeline_output_set_bt(bool enable_bt, audio_pipeline_handle_t pipeline, audio_element_handle_t **output_stream_writer,
                                  const char **link_tag, int link_num)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);
    ESP_LOGE(TAG, "Changing output");

    //return if output device is same as need
    if (output_is_bt == enable_bt) {
        return ESP_OK;
    }

    output_is_bt = enable_bt;

    if (!pipeline) {
        return ESP_OK;
    }

    //audio_pipeline_pause(main_pipeline);
    ESP_LOGI(TAG, "break pipeline");
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_elements(pipeline);
    audio_pipeline_breakup_elements(pipeline, NULL);

    audio_pipeline_change_state(pipeline, AEL_STATE_INIT);

    ESP_LOGI(TAG, "Unregister current %s stream writer", output_stream_name);
    if (output_stream != NULL) {
        audio_pipeline_unregister(pipeline, *output_stream);
        audio_element_deinit(*output_stream);
    }

    if (!output_is_bt) {
        bt_deinit();
        bt_stream_writer = NULL;
    }

    pipeline_output_init_stream(output_stream_writer, output_stream_name);

    ESP_LOGI(TAG, "Register %s stream writer", output_stream_name);
    audio_pipeline_register(pipeline, *output_stream, output_stream_name);

    ESP_LOGI(TAG, "Link tags");
    audio_pipeline_relink(pipeline, link_tag, link_num);
    //audio_pipeline_set_listener(pipeline, evt);
    ESP_LOGI(TAG, "[-] Listening event from pipelines");
    ESP_ERROR_CHECK(audio_pipeline_set_listener(pipeline, evt));

    //audio_pipeline_reset_ringbuffer(pipeline);
    //audio_pipeline_reset_elements(pipeline);
    //audio_pipeline_change_state(pipeline, AEL_STATE_INIT);
    audio_pipeline_run(pipeline);
    audio_pipeline_resume(pipeline);


    //ESP_LOGE(TAG, "[ 4.1 ] Start playback new pipeline");

    return ESP_OK;
}

void pipeline_output_deinit(audio_pipeline_handle_t pipeline, audio_element_handle_t **stream)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    if (!pipeline) {
        return;
    }
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);

    audio_pipeline_terminate(pipeline);
    audio_pipeline_remove_listener(pipeline);
    *stream = NULL;
    output_stream = NULL;

    audio_pipeline_deinit(pipeline);

    if (output_is_bt) {
        bt_deinit();
        bt_stream_writer = NULL;
    } else {
        i2s_stream_writer = NULL;
    }
}