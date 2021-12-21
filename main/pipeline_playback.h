//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_PLAYBACK_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_PLAYBACK_H

#include <esp_err.h>
#include <audio_event_iface.h>
#include <audio_pipeline.h>

esp_err_t pipeline_playback_stop(void);
esp_err_t pipeline_playback_start(audio_event_iface_handle_t evt, const char *filename);
esp_err_t pipeline_playback_event_loop(audio_event_iface_handle_t evt);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_PLAYBACK_H
