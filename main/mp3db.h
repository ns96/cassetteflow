//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_MP3DB_H
#define CASSETTEFLOW_FIRMWARE_MAIN_MP3DB_H

#define MP3DB_MAX_LINE_LENGTH       (1024)

esp_err_t mp3db_scan(void);
esp_err_t mp3db_stop(void);
esp_err_t mp3db_file_for_id(const char *mp3id, char *filepath, int *duration);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_MP3DB_H
