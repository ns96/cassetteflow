//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 12.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_MP3INFO_H
#define CASSETTEFLOW_FIRMWARE_MAIN_MP3INFO_H

#include <stdio.h>

int mp3info_get_info(const char *filepath, int *duration, int *avg_bitrate);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_MP3INFO_H
