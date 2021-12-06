//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <stdio.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <string.h>
#include <esp_peripherals.h>
#include <periph_wifi.h>
#include "internal.h"

#include "network.h"
#include "config.h"

static const char *TAG = "cf_network";

static esp_periph_handle_t wifi_handle;

/**
 * Read wifi configuration from file
 * @param wifi_ssid
 * @param wifi_password
 * @return 0 - OK, -1 - Error
 */
static int network_read_config(char *wifi_ssid, char *wifi_password)
{
    int ret = 0;
    struct stat file_stat;
    FILE *fd = NULL;

    if (stat(FILE_WIFI_CONFIG, &file_stat) == -1) {
        ESP_LOGW(TAG, "WiFi config file not found. using default settings");
        return -1;
    }

    fd = fopen(FILE_WIFI_CONFIG, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open file : %s", FILE_WIFI_CONFIG);
        return -1;
    }

    // max SSID is 32 characters
    // max WPA password is 63 characters
    if (fscanf(fd, "%32s\t%63s", wifi_ssid, wifi_password) != 2) {
        ESP_LOGE(TAG, "WiFi config file: invalid format");
        ret = -1;
    }
    fclose(fd);
    return ret;
}

esp_err_t network_connect(void)
{
    char wifi_ssid[33] = {0};
    char wifi_password[64] = {0};
    periph_wifi_cfg_t wifi_cfg = {0};

    ESP_LOGI(TAG, "connect");

    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    if (network_read_config(wifi_ssid, wifi_password) == 0) {
        wifi_cfg.ssid = wifi_ssid;
        wifi_cfg.password = wifi_password;
    } else {
        wifi_cfg.ssid = CONFIG_WIFI_SSID;
        wifi_cfg.password = CONFIG_WIFI_PASSWORD;
    }
    wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    return periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);
}

esp_err_t network_disconnect(void)
{
    ESP_LOGI(TAG, "disconnect");

    return esp_periph_stop(wifi_handle);
}
