//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_H

#include <esp_err.h>
#include "esp_event.h"
#include "audio_pipeline.h"
#include "internal.h"

extern esp_event_loop_handle_t pipeline_event_loop;

ESP_EVENT_DECLARE_BASE(PIPELINE_EVENTS);         // declaration of the task events family

enum
{
    PIPELINE_ENCODE_STARTED = 0,
    PIPELINE_DECODE_STARTED = 1,
    PIPELINE_PASSTHROUGH_STARTED = 2,
    PIPELINE_PLAYBACK_STARTED = 3,
};

enum pipeline_decoder_mode
{
    PIPELINE_DECODER_MP3 = 0,
    PIPELINE_DECODER_FLAC = 1,
};

void pipeline_set_side(const char side);
void pipeline_handle_play(void);
void pipeline_handle_set(void);
void pipeline_set_mode(enum cf_mode mode);
void pipeline_current_info_str(char *str, size_t str_len);
esp_err_t pipeline_start_encoding(char side);
esp_err_t pipeline_stop(void);
esp_err_t pipeline_init(audio_event_iface_handle_t event_handle);
esp_err_t pipeline_main(void);
esp_err_t pipeline_set_equalizer(int band_gain[10]);
esp_err_t pipeline_start_playing(const char side);
esp_err_t pipeline_set_output_bt(bool enable, const char *device);
void pipeline_unpause(void);
void pipeline_pause(void);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_H
