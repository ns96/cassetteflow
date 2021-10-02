//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_err.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_peripherals.h>
#include <board.h>
#include "main.h"
#include "http_server.h"
#include "network.h"
#include "keys.h"
#include "mp3db.h"

static const char *TAG = "cf_main";

void app_main(void)
{
    // FIXME change to INFO after debugging
    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "[1.0] Initialize peripherals management");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[1.1] Initialize and start peripherals");
    audio_board_key_init(set);
    audio_board_sdcard_init(set, SD_MODE_4_LINE);

    ESP_LOGI(TAG, "[1.3] Scan for new MP3 files on SD card");
    ESP_ERROR_CHECK(mp3db_scan());

    ESP_LOGI(TAG, "[ 2 ] Init board");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH,
                         AUDIO_HAL_CTRL_START);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "[ 3 ] Connect to the network");
    ESP_ERROR_CHECK(network_connect());

    ESP_LOGI(TAG, "[ 4 ] Create and start HTTP server");
    ESP_ERROR_CHECK(http_server_start());

    ESP_LOGI(TAG, "[ 5 ] Create and start input key service");
    ESP_ERROR_CHECK(keys_start(set, board_handle));

    // main loop
    while (1);

    esp_periph_set_destroy(set);
    keys_stop();
}
