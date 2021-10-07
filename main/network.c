//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <string.h>
#include <esp_peripherals.h>
#include <periph_wifi.h>

#include "network.h"
#include "config.h"

#define NR_OF_IP_ADDRESSES_TO_WAIT_FOR (s_active_interfaces)

static const char *TAG = "cf_network";

static esp_periph_handle_t wifi_handle;

esp_err_t network_connect(void)
{
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    periph_wifi_cfg_t wifi_cfg = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };
    wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    return periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);
}

esp_err_t network_disconnect(void)
{
    return esp_periph_stop(wifi_handle);
}
