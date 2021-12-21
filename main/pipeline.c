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
#include "pipeline_passthrough.h"
#include "tapefile.h"
#include "tapedb.h"
#include "eq.h"
#include "led.h"

static const char *TAG = "cf_pipeline";

esp_event_loop_handle_t pipeline_event_loop;

/* Event source task related definitions */
ESP_EVENT_DEFINE_BASE(PIPELINE_EVENTS);

static enum cf_mode pipeline_mode = MODE_DECODE;
static char current_encoding_side;
static audio_event_iface_handle_t evt;

static void pipeline_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id) {
        case PIPELINE_ENCODE_STARTED: {
            bool encoding_finished = pipeline_encode_event_loop(evt);
            if (encoding_finished) {
                // encode finished, save to the database
                tapedb_file_save(current_encoding_side);
            }
            break;
        }
        case PIPELINE_DECODE_STARTED:
            pipeline_decode_event_loop(evt);
            break;
        case PIPELINE_PASSTHROUGH_STARTED:
            pipeline_passthrough_event_loop(evt);
            break;
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
    ESP_LOGI(TAG, "handle PLAY key");

    switch (pipeline_mode) {
        case MODE_DECODE:
            // In DECODE mode, will switch between playing the mp3 file
            // indicated by the data read from the cassette tape (default),
            // or outputting the raw audio data from cassette to the headphone output.
            // It does not control the playback of mp3 files.
            pipeline_set_mode(MODE_PASSTHROUGH);
            break;
        case MODE_ENCODE:
            // In ENCODE mode, pressing it will stop the encoding process, just like sending the “STOP” command,
            // if an encoding is in progress. If no encoding is in progress and the mixtape file (SideA.txt by default)
            // is present, start the encoding process.
            if (pipeline_encode_is_running()) {
                pipeline_stop_encoding();
            } else {
                // check if mixtape file is present
                if (tapefile_is_present(current_encoding_side)) {
                    pipeline_start_encoding(current_encoding_side);
                }
            }
            break;
        case MODE_PASSTHROUGH:
            pipeline_set_mode(MODE_DECODE);
            break;
        default:
            assert(0);
            break;
    }
}

void pipeline_handle_set(void)
{
    ESP_LOGI(TAG, "handle SET key");
    eq_set_key_pressed();
}

void pipeline_set_mode(enum cf_mode mode)
{
    ESP_LOGI(TAG, "set_mode: %d", mode);

    //stop current mode
    switch (pipeline_mode) {
        case MODE_DECODE:
            pipeline_decode_stop();
            break;
        case MODE_ENCODE:
            pipeline_stop_encoding();
            break;
        case MODE_PASSTHROUGH:
            pipeline_passthrough_stop();
            break;
        default:
            assert(0);
            break;
    }

    //start new mode
    switch (mode) {
        case MODE_DECODE:
            pipeline_decode_start(evt);
            break;
        case MODE_ENCODE:
            // nothing here, started with a separate command
            break;
        case MODE_PASSTHROUGH:
            pipeline_passthrough_start(evt);
            break;
        default:
            assert(0);
            break;
    }

    pipeline_mode = mode;
}

void pipeline_current_info_str(char *str, size_t str_len)
{
    switch (pipeline_mode) {
        case MODE_DECODE:
            pipeline_decode_status(str, str_len);
            break;
        case MODE_ENCODE:
            pipeline_encode_status(current_encoding_side, str, str_len);
            break;
        case MODE_PASSTHROUGH:
            snprintf(str, str_len, "passthrough");
            break;
        default:
            assert(0);
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

    // switch to ENCODE mode if needed
    pipeline_set_mode(MODE_ENCODE);

    return pipeline_encode_start(evt, file_uri);
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


esp_err_t pipeline_set_equalizer(int band_gain[10])
{
    switch (pipeline_mode) {
        case MODE_DECODE:
            return pipeline_decode_set_equalizer(band_gain);
        case MODE_ENCODE:
            break;
        case MODE_PASSTHROUGH:
            return pipeline_passthrough_set_equalizer(band_gain);
            break;
        case MODE_PLAYBACK:
            break;
        default:
            assert(0);
            break;
    }
    return ESP_OK;
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
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(pipeline_event_loop, PIPELINE_EVENTS,
                                                             PIPELINE_DECODE_STARTED, pipeline_event_handler,
                                                             NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(pipeline_event_loop, PIPELINE_EVENTS,
                                                             PIPELINE_PASSTHROUGH_STARTED, pipeline_event_handler,
                                                             NULL, NULL));

    if (pipeline_mode == MODE_DECODE) {
        pipeline_decode_start(evt);
    }

    return evt != NULL ? ESP_OK : ESP_FAIL;
}

/**
 * Main pipeline loop
 * @return
 */
esp_err_t pipeline_main(void)
{
    // delay 1 second
    vTaskDelay(pdMS_TO_TICKS(1000));
    return ESP_OK;
}
