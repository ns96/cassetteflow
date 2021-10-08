//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_H

#include <esp_err.h>
#include "esp_event.h"
#include "internal.h"

enum
{
    APP_MSG_ENCODE_FINISHED = 0,
};

void pipeline_set_side(const char side);
void pipeline_handle_play(void);
void pipeline_set_mode(enum cf_mode mode);
void pipeline_current_info_str(char *str, size_t str_len);
esp_err_t pipeline_start_encoding(char side);
esp_err_t pipeline_stop_encoding();
esp_err_t pipeline_init(void);
esp_err_t pipeline_main(void);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_H
