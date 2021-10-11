//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_LED_H
#define CASSETTEFLOW_FIRMWARE_MAIN_LED_H

#include <esp_err.h>

esp_err_t led_init(void);
esp_err_t led_eq_on(void);
esp_err_t led_eq_off(void);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_LED_H
