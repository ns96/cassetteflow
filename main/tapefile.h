//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_TAPEFILE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_TAPEFILE_H

#include <esp_err.h>

const char *tapefile_get_path(const char side);
const char *tapefile_get_path_tapedb(const char side);
esp_err_t tapefile_create(const char side, int tape_length_minutes, char *data, int mute_time);
bool tapefile_is_present(const char side);
esp_err_t tapefile_read_tapeid(const char side, char *tapeid);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_TAPEFILE_H
