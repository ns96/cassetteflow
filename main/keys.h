//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_KEYS_H
#define CASSETTEFLOW_FIRMWARE_MAIN_KEYS_H

esp_err_t keys_start(esp_periph_set_handle_t esp_periph_set_handle, audio_board_handle_t board_handle);
esp_err_t keys_stop(void);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_KEYS_H
