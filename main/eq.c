//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#include <esp_log.h>
#include "eq.h"

static const char *TAG = "cf_eq";

/**
 * Read from CSV file
 * @param bands
 * @return
 */
esp_err_t eq_read_from_file(const char *filepath, int bands[10])
{
    FILE *fd = NULL;
    esp_err_t ret = ESP_FAIL;

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