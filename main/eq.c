//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#include <esp_log.h>
#include "eq.h"
#include "led.h"
#include "internal.h"
#include <audio_event_iface.h>
#include "pipeline_decode.h"

static const char *TAG = "cf_eq";
static bool eq_active = false;

/**
 * Read from CSV file
 * @param bands
 * @return ESP_OK or error
 */
esp_err_t eq_read_from_file(const char *filepath, int bands[10])
{
    FILE *fd = NULL;
    esp_err_t ret = ESP_OK;

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        return ESP_FAIL;
    }

    if (fscanf(fd, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &bands[0], &bands[1], &bands[2], &bands[3], &bands[4],
               &bands[5], &bands[6], &bands[7], &bands[8], &bands[9]) != 10) {
        ret = ESP_FAIL;
    }

    fclose(fd);

    return ret;
}

/**
 * If the EQ is not on then pressing SET reads from the eq.txt file and turns it on.
 * If the EQ has been set using the http endpoint then pressing SET should turn the EQ off.
 * Green LED is ON when EQ is active
 * @return ESP_OK or error
 */
esp_err_t eq_set_key_pressed(void)
{
    esp_err_t ret = ESP_OK;
    const char *filename = FILE_EQ;
    int bands[10] = {0}; // default EQ (off)

    if (!eq_active) {
        // read EQ preset from file
        ret = eq_read_from_file(filename, bands);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error reading EQ data from file");
        }
    }

    if (ret == ESP_OK) {
        ret = eq_process_bands(bands);
    }
    return ret;
}

/**
 * Turn ON/OFF EQ
 * @param bands array of 10 values for 10 eq bands
 * @return ESP_OK or error
 */
esp_err_t eq_process_bands(int bands[10])
{
    if (pipeline_decode_set_equalizer(bands) != ESP_OK) {
        ESP_LOGE(TAG, "Error setting EQ data");
        return ESP_FAIL;
    }

    bool all_bands_are_zero = true;

    //check all bands
    for (int i = 0; i < 10; ++i) {
        if (bands[i] != 0) {
            all_bands_are_zero = false;
            break;
        }
    }

    // update eq_active only after actual EQ update in pipeline_decode_set_equalizer()
    eq_active = !all_bands_are_zero;

    // turn on green LED if EQ is active
    if (eq_active) {
        led_eq_on();
    } else {
        led_eq_off();
    }
    return ESP_OK;
}
