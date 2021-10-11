//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_DECODE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_DECODE_H

esp_err_t pipeline_decode_start(audio_event_iface_handle_t evt);
esp_err_t pipeline_decode_event_loop(audio_event_iface_handle_t evt);
esp_err_t pipeline_decode_stop(void);
void pipeline_decode_status(char *buf, size_t buf_size);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_DECODE_H
