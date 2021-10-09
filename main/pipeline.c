//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#include <esp_log.h>
#include <string.h>
#include <audio_event_iface.h>
#include <audio_common.h>
#include <esp_peripherals.h>
#include "pipeline.h"
#include "pipeline_decode.h"
#include "pipeline_encode.h"
#include "tapefile.h"
#include "tapedb.h"

static const char *TAG = "cf_pipeline";

esp_event_loop_handle_t pipeline_event_loop;

/* Event source task related definitions */
ESP_EVENT_DEFINE_BASE(PIPELINE_EVENTS);

static enum cf_mode pipeline_mode = MODE_DECODE;
static enum cf_pipeline_state pipeline_state = PIPELINE_STOPPED;
static char current_encoding_side;
static audio_event_iface_handle_t evt;

static void pipeline_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id) {
        case PIPELINE_ENCODE_STARTED:
            pipeline_encode_event_loop(evt);
            // encode finished, save to the database
            tapedb_file_save(current_encoding_side);
            break;
    }
}

void pipeline_set_side(const char side)
{
    ESP_LOGI(TAG, "set_side: %c", side);

    if (pipeline_mode == MODE_DECODE || !pipeline_encode_is_running()) {
        current_encoding_side = side;
    }
}

void pipeline_handle_play(void)
{
    ESP_LOGI(TAG, "handle play");

    switch (pipeline_mode) {
        case MODE_DECODE:
            // TODO
            break;
        case MODE_ENCODE:
            if (pipeline_encode_is_running()) {
                pipeline_stop_encoding();
            } else {
                // check if mixtape file is present
                if (tapefile_is_present(current_encoding_side)) {
                    pipeline_start_encoding(current_encoding_side);
                }
            }
            break;
    }
}

void pipeline_set_mode(enum cf_mode mode)
{
    ESP_LOGI(TAG, "set_mode: %d", mode);

    //stop current mode
    if (pipeline_mode == MODE_DECODE) {
        pipeline_decode_stop();
    } else {
        pipeline_encode_stop();
        // TODO start MODE_DECODE
    }

    pipeline_mode = mode;
}

void pipeline_current_info_str(char *str, size_t str_len)
{
    // TODO
    //generate response string for each mode
    switch (pipeline_mode) {
        case MODE_DECODE:
            if (pipeline_state == PIPELINE_STOPPED) {
                strcpy(str, "playback stopped");
                break;
            }
            pipeline_decode_status(str, sizeof(str));
            break;
        case MODE_ENCODE:
            if (pipeline_state == PIPELINE_STOPPED) {
                strcpy(str, "encoded stopped");
                break;
            } else if (pipeline_state == PIPELINE_ENCODE_COMPLETED) {
                strcpy(str, "encoded completed");
                break;
            }
            pipeline_encode_status(str, sizeof(str));
            break;
    }
}

/**
 * Only for MODE_ENCODE
 * @return
 */
esp_err_t pipeline_start_encoding(const char side)
{
    ESP_LOGI(TAG, "start_encoding");

    char file_uri[128];

    current_encoding_side = side;

    snprintf(file_uri, sizeof(file_uri), "file:/%s", tapefile_get_path(side));

    if (pipeline_mode == MODE_ENCODE) {
        return pipeline_encode_start(evt, file_uri);
    } else {
        return ESP_FAIL;
    }
}

/**
 * Only for MODE_ENCODE
 * @return
 */
esp_err_t pipeline_stop_encoding()
{
    ESP_LOGI(TAG, "stop_encoding");

    if (pipeline_mode == MODE_ENCODE) {
        // stop audio pipeline
        return pipeline_encode_stop();
    } else {
        return ESP_FAIL;
    }
}

esp_err_t pipeline_init(audio_event_iface_handle_t event_handle)
{
    evt = event_handle;

    esp_event_loop_args_t loop_args = {
        .queue_size = 5,
        .task_name = "pipeline_loop_task", // task will be created
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY
    };

    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &pipeline_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(pipeline_event_loop, PIPELINE_EVENTS,
                                                             PIPELINE_ENCODE_STARTED, pipeline_event_handler,
                                                             NULL, NULL));

    if (pipeline_mode == MODE_DECODE) {
        pipeline_decode_start();
    }

    return evt != NULL ? ESP_OK : ESP_FAIL;
}

/**
 * Main pipeline loop
 * @return
 */
esp_err_t pipeline_main(void)
{
    vTaskDelay(100);
    return ESP_OK;
}
