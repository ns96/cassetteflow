//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 11.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_PASSTHROUGH_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_PASSTHROUGH_H

#include <esp_err.h>
#include <audio_event_iface.h>

esp_err_t pipeline_passthrough_start(audio_event_iface_handle_t evt);
esp_err_t pipeline_passthrough_event_loop(audio_event_iface_handle_t evt);
esp_err_t pipeline_passthrough_stop(void);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_PASSTHROUGH_H
