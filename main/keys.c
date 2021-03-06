//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_err.h>
#include <periph_service.h>
#include <esp_log.h>
#include <input_key_service.h>
#include "keys.h"
#include "pipeline.h"
#include "volume.h"

static const char *TAG = "cf_keys";

static periph_service_handle_t input_ser;

static esp_err_t input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    /* Handle key events to start, pause, resume, finish current song and adjust volume */
    audio_board_handle_t board_handle = (audio_board_handle_t)ctx;

    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        ESP_LOGD(TAG, "[ * ] input key id is %d", (int)evt->data);
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_REC:
                ESP_LOGI(TAG, "[ * ] [F1] input key event");
                pipeline_set_side('a');
                break;
            case INPUT_KEY_USER_ID_MODE:
                ESP_LOGI(TAG, "[ * ] [F2] input key event");
                pipeline_set_side('b');
                break;
            case INPUT_KEY_USER_ID_PLAY:
                ESP_LOGI(TAG, "[ * ] [Play] input key event");
                pipeline_handle_play();
                break;
            case INPUT_KEY_USER_ID_SET:
                ESP_LOGI(TAG, "[ * ] [Set] input key event");
                pipeline_handle_set();
                break;
            case INPUT_KEY_USER_ID_VOLUP:
                ESP_LOGI(TAG, "[ * ] [Vol+] input key event");
                volume_set(10);
                break;
            case INPUT_KEY_USER_ID_VOLDOWN:
                ESP_LOGI(TAG, "[ * ] [Vol-] input key event");
                volume_set(-10);
                break;
        }
    }

    return ESP_OK;
}

esp_err_t keys_start(esp_periph_set_handle_t esp_periph_set_handle, audio_board_handle_t board_handle)
{
    ESP_LOGI(TAG, "start");

    input_key_service_info_t input_key_info[] = INPUT_KEY_DEFAULT_INFO();
    input_key_service_cfg_t input_cfg = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
    input_cfg.handle = esp_periph_set_handle;
    input_ser = input_key_service_create(&input_cfg);
    input_key_service_add_key(input_ser, input_key_info, INPUT_KEY_NUM);
    periph_service_set_callback(input_ser, input_key_service_cb, (void *)board_handle);

    return ESP_OK;
}

esp_err_t keys_stop(void)
{
    ESP_LOGI(TAG, "stop");

    periph_service_destroy(input_ser);
    return ESP_OK;
}
