//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_OUTPUT_H
#define CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_OUTPUT_H

#include <esp_err.h>
#include <audio_event_iface.h>
#include <audio_pipeline.h>

esp_err_t pipeline_output_init_stream(audio_element_handle_t *output_stream_writer,
                                      char **stream_name);

void pipeline_output_deinit(audio_pipeline_handle_t pipeline, audio_element_handle_t *stream);

char *pipeline_output_get_stream_name(void);

bool pipeline_output_is_bt(void);

void pipeline_output_set_bt(bool enabled);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_PIPELINE_OUTPUT_H
