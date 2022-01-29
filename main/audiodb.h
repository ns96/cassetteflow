//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_AUDIODB_H
#define CASSETTEFLOW_FIRMWARE_MAIN_AUDIODB_H

#define AUDIODB_MAX_LINE_LENGTH       (1024)

esp_err_t audiodb_scan(void);
esp_err_t audiodb_stop(void);
esp_err_t audiodb_file_for_id(const char *audioid, char *filepath, int *duration, int *avg_bitrate);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_AUDIODB_H
