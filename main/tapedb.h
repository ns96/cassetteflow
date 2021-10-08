//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_TAPEDB_H
#define CASSETTEFLOW_FIRMWARE_MAIN_TAPEDB_H

#include <esp_err.h>

esp_err_t tapedb_file_save(const char side);
bool tapedb_tape_exists(const char *tape);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_TAPEDB_H
