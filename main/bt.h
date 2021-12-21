//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_BT_H
#define CASSETTEFLOW_FIRMWARE_MAIN_BT_H

#include <esp_err.h>
#include <audio_event_iface.h>
#include "bluetooth_service.h"

esp_err_t bt_periph_init(audio_event_iface_handle_t evt);
esp_err_t bt_init_output_stream(audio_element_handle_t *stream_writer);
int bt_process_events(audio_event_iface_msg_t msg);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_BT_H
