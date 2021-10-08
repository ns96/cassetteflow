//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#include <esp_log.h>
#include <string.h>
#include <audio_event_iface.h>
#include <audio_common.h>
#include "pipeline.h"
#include "pipeline_decode.h"
#include "pipeline_encode.h"
#include "tapefile.h"
#include "tapedb.h"

static const char *TAG = "cf_pipeline";

static enum cf_mode pipeline_mode = MODE_DECODE;
static enum cf_pipeline_state pipeline_state = PIPELINE_STOPPED;
static char current_encoding_side;
static audio_event_iface_handle_t evt;

void pipeline_set_side(const char side)
{
    if (pipeline_mode == MODE_DECODE || !pipeline_encode_is_running()) {
        current_encoding_side = side;
    }
}

void pipeline_handle_play(void)
{
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
    if (pipeline_mode == MODE_ENCODE) {
        // stop audio pipeline
        return pipeline_encode_stop();
    } else {
        return ESP_FAIL;
    }
}

esp_err_t pipeline_init(void)
{
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);

    return evt != NULL ? ESP_OK : ESP_FAIL;
}

/**
 * Main pipeline loop
 * @return
 */
esp_err_t pipeline_main(void)
{
    audio_event_iface_msg_t msg;
    esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
        return ESP_FAIL;
    }

    if (msg.source_type == AUDIO_ELEMENT_TYPE_PLAYER) {
        // handle events
        switch (msg.cmd) {
            case APP_MSG_ENCODE_FINISHED:
                // stop audio pipeline
                pipeline_stop_encoding();
                // save to the database
                tapedb_file_save(current_encoding_side);
                break;
        }
    }

    switch (pipeline_mode) {
        case MODE_DECODE:
            break;
        case MODE_ENCODE:
            pipeline_encode_maybe_handle_event(evt, &msg);
            break;
    }

    return ESP_OK;
}
