//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_INTERNAL_H
#define CASSETTEFLOW_FIRMWARE_MAIN_INTERNAL_H

enum cf_mode
{
    MODE_DECODE = 0,
    MODE_ENCODE = 1,
};

enum cf_pipeline_state
{
    PIPELINE_STOPPED = 0,
    PIPELINE_STARTED,
    PIPELINE_ENCODE_COMPLETED
};

#define FILENAME_SIDE_A     "/sdcard/sideA.txt"
#define FILENAME_SIDE_B     "/sdcard/sideB.txt"
// files with a line for tapeDB (in the same format)
#define FILENAME_SIDE_A_TAPEDB     "/sdcard/sideA_tapedb.txt"
#define FILENAME_SIDE_B_TAPEDB     "/sdcard/sideB_tapedb.txt"

#define FILE_MP3DB      "/sdcard/mp3db.txt"
#define FILE_TAPEDB     "/sdcard/tapedb.txt"

#endif //CASSETTEFLOW_FIRMWARE_MAIN_INTERNAL_H
