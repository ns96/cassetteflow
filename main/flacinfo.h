//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_FLACINFO_H
#define CASSETTEFLOW_FIRMWARE_MAIN_FLACINFO_H

int flacinfo_get_info(const char *filepath, int *duration, int *avg_bitrate);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_FLACINFO_H
