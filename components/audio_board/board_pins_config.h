#ifndef _BOARD_PINS_CONFIG_H_CUSTOM_
#define _BOARD_PINS_CONFIG_H_CUSTOM_

#include "board_def.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "driver/spi_master.h"

esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_config);
esp_err_t get_i2s_pins(i2c_port_t port, i2s_pin_config_t *i2s_config);
esp_err_t get_spi_pins(spi_bus_config_t *bus_config, spi_device_interface_config_t *clk_config);
int get_pa_enable_gpio(void);
int get_sdcard_intr_gpio(void);
int get_sdcard_open_file_num_max(void);
int get_input_vol_id(void);
int get_reset_board_gpio(void);
int get_es7243_mclk_gpio(void);

#endif
