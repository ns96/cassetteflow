//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_H

#include <esp_err.h>
#include "internal.h"

void pipeline_set_mode(enum cf_mode mode);
void pipeline_current_info_str(char *str, size_t str_len);
esp_err_t pipeline_start_encoding(char side);
esp_err_t pipeline_stop_encoding();

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_H
