//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_TAPEFILE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_TAPEFILE_H

#include <esp_err.h>

const char *tapefile_get_path(const char side);
const char *tapefile_get_path_tapedb(const char side);
esp_err_t tapefile_create(const char side, const char *tape, char *data, int mute_time);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_TAPEFILE_H
