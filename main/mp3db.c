//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_err.h>
#include <esp_log.h>
#include <sdcard_list.h>
#include <sdcard_scan.h>
#include "mp3db.h"

static const char *TAG = "cf_mp3db";

playlist_operator_handle_t sdcard_list_handle = NULL;

static void sdcard_url_save_cb(void *user_data, char *url)
{
    playlist_operator_handle_t sdcard_handle = (playlist_operator_handle_t)user_data;
    esp_err_t ret = sdcard_list_save(sdcard_handle, url);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fail to save sdcard url to sdcard playlist");
    }
}

// scan for new mp3 files on the SD card
esp_err_t mp3db_scan(void)
{
    sdcard_list_create(&sdcard_list_handle);
    // scan for mp3 files
    sdcard_scan(sdcard_url_save_cb, "/sdcard", 0, (const char *[]){"mp3"},
                1, sdcard_list_handle);

    // TODO

    sdcard_list_show(sdcard_list_handle);

    return ESP_OK;
}

esp_err_t mp3db_stop(void)
{
    sdcard_list_destroy(sdcard_list_handle);

    return ESP_OK;
}