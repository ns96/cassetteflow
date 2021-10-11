//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#include <esp_log.h>
#include <board.h>
#include "led.h"

static const char *TAG = "cf_led";

static display_service_handle_t disp = NULL;

esp_err_t led_init(void)
{
    disp = audio_board_led_init();

    return disp != NULL ? ESP_OK : ESP_FAIL;
}

esp_err_t led_eq_on(void)
{
    return display_service_set_pattern(disp, DISPLAY_PATTERN_TURN_ON, 0);
}

esp_err_t led_eq_off(void)
{
    return display_service_set_pattern(disp, DISPLAY_PATTERN_TURN_OFF, 0);
}
