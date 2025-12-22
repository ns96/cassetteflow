#ifndef _AUDIO_BOARD_H_CUSTOM_
#define _AUDIO_BOARD_H_CUSTOM_

#include "audio_hal.h"
#include "display_service.h"
#include "board_def.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "driver/i2s.h"

#ifdef __cplusplus
extern "C" {
#endif

struct audio_board_handle {
    audio_hal_handle_t audio_hal;
    esp_periph_set_handle_t key_set; // Handle to key set (esp_peripherals)
};
typedef struct audio_board_handle *audio_board_handle_t;

/**
 * @brief Initialize audio board
 *
 * @return The audio board handle
 */
audio_board_handle_t audio_board_init(void);

/**
 * @brief Initialize audio board SD card
 *
 * @param set The handle of esp_peripherals
 * @param mode SD card mode
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t audio_board_sdcard_init(esp_periph_set_handle_t set, periph_sdcard_mode_t mode);

/**
 * @brief Initialize audio board keys
 *
 * @param set The handle of esp_peripherals
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t audio_board_key_init(esp_periph_set_handle_t set);

/**
 * @brief Initialize audio board LEDs (Display Service)
 *
 * @return The display service handle
 */
display_service_handle_t audio_board_led_init(void);

/**
 * @brief Get the audio board handle
 *
 * @return The audio board handle
 */
audio_board_handle_t audio_board_get_handle(void);

#ifdef __cplusplus
}
#endif

#endif
int get_reset_board_gpio(void);
int get_es7243_mclk_gpio(void);
esp_err_t get_spi_pins(spi_bus_config_t *bus_config, spi_device_interface_config_t *clk_config);
esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_config);
esp_err_t get_i2s_pins(i2c_port_t port, i2s_pin_config_t *i2s_config);
int get_pa_enable_gpio(void);
int get_sdcard_intr_gpio(void);
int get_sdcard_open_file_num_max(void);
int get_input_vol_id(void);
