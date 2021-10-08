//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_ENCODE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_ENCODE_H

esp_err_t pipeline_encode_start(char *url);
esp_err_t pipeline_encode_stop(void);
esp_err_t pipeline_encode_mix(char *url);
void pipeline_encode_status(char *buf, int buf_size);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_ENCODE_H
