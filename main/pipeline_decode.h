//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_DECODE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_DECODE_H

esp_err_t pipeline_decode_start(void);
esp_err_t pipeline_decode_maybe_handle_event(audio_event_iface_handle_t evt, audio_event_iface_msg_t *msg);
esp_err_t pipeline_decode_stop(void);
void pipeline_decode_status(char *buf, int buf_size);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_DECODE_H
