//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_EQ_H
#define CASSETTEFLOW_FIRMWARE_MAIN_EQ_H

#include <esp_err.h>

esp_err_t eq_read_from_file(const char *filepath, int bands[10]);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_EQ_H
