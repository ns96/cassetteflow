//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#include "volume.h"
#include "board.h"
#include <esp_log.h>

static const char *TAG = "cf_volume";

extern audio_board_handle_t board_handle;

/**
 * Change the volume by the specific value. A positive value increase volume,
 * while a negative value moves it down. Send a value of 0 will mute the device,
 * if it not already muted.
 * If it’s already muted then the volume should be set to the previous value.
 * @param value
 * @return ESP_OK or error
 */
esp_err_t volume_set(int value)
{
    static bool muted = false;
    int audio_volume = 0;

    ESP_LOGI(TAG, "[ * ] value: %d", value);

    audio_hal_get_volume(board_handle->audio_hal, &audio_volume);

    ESP_LOGI(TAG, "[ * ] audio_volume: %d", audio_volume);

    if (value > 0) {
        audio_volume += value;
        if (audio_volume > 100) {
            audio_volume = 100;
        }
    } else if (value < 0) {
        audio_volume += value;
        if (audio_volume < 0) {
            audio_volume = 0;
        }
    } else {
        //mute or unmute
        muted = !muted;
        ESP_LOGI(TAG, "[ * ] Mute set to %s", muted ? "true" : "false");
        audio_hal_set_mute(board_handle->audio_hal, muted);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "[ * ] Volume set to %d %%", audio_volume);
    if (audio_hal_set_volume(board_handle->audio_hal, audio_volume) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}
