//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_INTERNAL_H
#define CASSETTEFLOW_FIRMWARE_MAIN_INTERNAL_H

enum cf_mode
{
    // play the mp3 file indicated by the data read from the cassette tape
    MODE_DECODE = 0,
    // encode data
    MODE_ENCODE = 1,
    // output the raw audio data from cassette to the headphone output
    MODE_PASSTHROUGH = 2,
    // play mp3 files as indicated by sideA.txt
    MODE_PLAYBACK = 3,
};

#define FILENAME_SIDE_A     "/sdcard/sideA.txt"
#define FILENAME_SIDE_B     "/sdcard/sideB.txt"
// files with a line for tapeDB (in the same format)
#define FILENAME_SIDE_A_TAPEDB     "/sdcard/sideA_tapedb.txt"
#define FILENAME_SIDE_B_TAPEDB     "/sdcard/sideB_tapedb.txt"

#define FILE_AUDIODB      "/sdcard/mp3db.txt"
#define FILE_TAPEDB     "/sdcard/tapedb.txt"

// equalizer preset file (CSV format, 10 bands)
#define FILE_EQ     "/sdcard/eq.txt"

#define FILE_WIFI_CONFIG "/sdcard/wifi_config.txt"

#endif //CASSETTEFLOW_FIRMWARE_MAIN_INTERNAL_H
