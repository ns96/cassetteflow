//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_BT_H
#define CASSETTEFLOW_FIRMWARE_MAIN_BT_H

#include <esp_err.h>
#include <audio_event_iface.h>
#include "bluetooth_service.h"

enum
{
    BT_SCAN_MODE = 0,
    BT_CONNECT_MODE = 1,
};

int bt_process_events(audio_event_iface_msg_t msg);
void bt_set_device(const char *device);
esp_err_t bt_init(void);
esp_err_t bt_connect_device(void);
esp_err_t bt_get_devices_list(void);
esp_err_t bt_deinit(void);
void bt_pause(void);
void bt_play(void);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_BT_H
