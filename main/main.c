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
#include "pipeline.h"
#include "led.h"
#include "raw_queue.h"

static const char *TAG = "cf_main";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());

    esp_log_level_set("*", ESP_LOG_INFO);
#if 1
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("AUDIO_PIPELINE", ESP_LOG_DEBUG);
#endif

    ESP_LOGI(TAG, "[1.0] Initialize peripherals management");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[1.1] Initialize and start peripherals");
    audio_board_key_init(set);
    audio_board_sdcard_init(set, SD_MODE_1_LINE);

    ESP_LOGI(TAG, "[1.2] Create LED service instance");
    led_init();

    ESP_LOGI(TAG, "[1.3] Scan for new MP3 files on SD card");
    ESP_ERROR_CHECK(mp3db_scan());

    ESP_LOGI(TAG, "[ 2 ] Init board");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE,
                         AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[ 3 ] Connect to the network");
    ESP_ERROR_CHECK(network_connect());

    ESP_LOGI(TAG, "[3.1] Create raw queue");
    ESP_ERROR_CHECK(raw_queue_init());

    ESP_LOGI(TAG, "[ 4 ] Create and start HTTP server");
    ESP_ERROR_CHECK(http_server_start());

    // FIXME uncomment
//    ESP_LOGI(TAG, "[ 5 ] Create and start input key service");
//    ESP_ERROR_CHECK(keys_start(set, board_handle));

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[ 6 ] Listening for events from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 7 ] Set up pipeline");
    pipeline_init(evt);

    ESP_LOGI(TAG, "[ 8 ] Main loop");
    while (1) {
        pipeline_main();
    }

    /* Stop all peripherals before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    keys_stop();
    esp_periph_set_destroy(set);
}
