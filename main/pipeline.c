//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#include <esp_log.h>
#include <string.h>
#include "pipeline.h"
#include "pipeline_decode.h"
#include "pipeline_encode.h"

static const char *TAG = "cf_pipeline";

static enum cf_mode pipeline_mode = MODE_DECODE;
static enum cf_pipeline_state pipeline_state = PIPELINE_STOPPED;

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
    char buf[64];

    return pipeline_encode_start(buf);
}

/**
 * Only for MODE_ENCODE
 * @return
 */
esp_err_t pipeline_stop_encoding()
{
    return ESP_OK;
}
