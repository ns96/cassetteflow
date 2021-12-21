//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_OUTPUT_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_OUTPUT_H

#include <esp_err.h>
#include <audio_event_iface.h>
#include <audio_pipeline.h>

esp_err_t pipeline_output_init_stream(audio_pipeline_handle_t pipeline, audio_element_handle_t *output_stream_writer,
                                      char *output_stream_name);
esp_err_t pipeline_output_set_bt(char *str, size_t str_len);
esp_err_t pipeline_output_set_sp(void);
esp_err_t pipeline_output_periph_start(audio_event_iface_handle_t evt);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_OUTPUT_H
