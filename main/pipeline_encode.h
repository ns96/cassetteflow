//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_ENCODE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_ENCODE_H

esp_err_t pipeline_encode_start(audio_event_iface_handle_t evt, char *url);
esp_err_t pipeline_encode_event_loop(audio_event_iface_handle_t evt);
esp_err_t pipeline_encode_stop();
void pipeline_encode_status(char *buf, int buf_size);
bool pipeline_encode_is_running(void);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_ENCODE_H
