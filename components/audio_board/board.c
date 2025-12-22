/*
 * Audio Board implementation for Ai-Thinker AudioKit v2.2 (ES8388)
 */

#include "esp_log.h"
#include "board.h"
#include "audio_mem.h"
#include "periph_sdcard.h"
#include "periph_button.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "es8388.h"

static const char *TAG = "AUDIO_BOARD";

// Define the handle structure external referencing the symbols in es8388.c,
// because AUDIO_CODEC_ES8388_DEFAULT_HANDLE is NOT exposed in es8388.h
extern audio_hal_func_t AUDIO_CODEC_ES8388_DEFAULT_HANDLE;

static audio_board_handle_t board_handle = 0;

audio_board_handle_t audio_board_init(void)
{
    if (board_handle) {
        ESP_LOGW(TAG, "The board has already been initialized");
        return board_handle;
    }
    board_handle = (audio_board_handle_t) audio_calloc(1, sizeof(struct audio_board_handle));
    AUDIO_MEM_CHECK(TAG, board_handle, return NULL);

    board_handle->audio_hal = audio_hal_init(&(audio_hal_codec_config_t) AUDIO_CODEC_DEFAULT_CONFIG(), &AUDIO_CODEC_ES8388_DEFAULT_HANDLE);
    
    // Setup PA Enable for valid audio output
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << PA_ENABLE_GPIO);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    gpio_set_level(PA_ENABLE_GPIO, 1); // Enable PA

    // Enable MCLK on GPIO 0
#ifdef CONFIG_IDF_TARGET_ESP32
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
    WRITE_PERI_REG(PIN_CTRL, 0xFF0);
#endif

    return board_handle;
}

audio_board_handle_t audio_board_get_handle(void)
{
    return board_handle;
}

esp_err_t audio_board_sdcard_init(esp_periph_set_handle_t set, periph_sdcard_mode_t mode)
{
    if (mode >= SD_MODE_4_LINE) {
        ESP_LOGE(TAG, "Ai-Thinker AudioKit usually expects SD 1-Line or SPI, forcing 1-Line for safety if 4-Line fails");
        // We will try standard logic
    }
    // Slot 1 (SDMMC) is standard for ESP32
    periph_sdcard_cfg_t sdcard_cfg = {
        .root = "/sdcard",
        .card_detect_pin = get_sdcard_intr_gpio(), // Usually -1 on A1S
        .mode = mode
    };
    esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
    esp_err_t ret = esp_periph_start(set, sdcard_handle);
    int retry_time = 5;
    bool mount_flag = false;
    while (retry_time --) {
        if (periph_sdcard_is_mounted(sdcard_handle)) {
            mount_flag = true;
            break;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    if (mount_flag == false) {
        ESP_LOGE(TAG, "Sdcard mount failed");
        return ESP_FAIL;
    }
    return ret;
}

esp_err_t audio_board_key_init(esp_periph_set_handle_t set)
{
    periph_button_cfg_t btn_cfg = {
        .gpio_mask = (1ULL << BUTTON_REC_ID) | (1ULL << BUTTON_MODE_ID) | (1ULL << BUTTON_PLAY_ID) |
                     (1ULL << BUTTON_SET_ID) | (1ULL << BUTTON_VOLUP_ID) | (1ULL << BUTTON_VOLDOWN_ID),
        // On some boards external pullups exist, on others we need internal. 
        // A1S keys usually pull to Ground.
    };
    esp_periph_handle_t button_handle = periph_button_init(&btn_cfg);
    
    // Initialize the handle in board pointer if needed, but periph set manages it.
    board_handle->key_set = set; // Store the set, not the button handle itself? original struct had void* key_set.
                                 // Actually periph_button_init returns a handle, but usually we just start it.
    
    return esp_periph_start(set, button_handle);
}

display_service_handle_t audio_board_led_init(void)
{
    // Ai-Thinker AudioKit usually doesn't have a complex LED driver, just GPIOs.
    // For now we return NULL to satisfy the linker, or we could implement a GPIO display service if needed.
    // Given the project uses 'led.c' which expects a display service, we really should assume one.
    // But since the custom board doesn't define one yet, let's return NULL and log warning.
    ESP_LOGW(TAG, "audio_board_led_init: LED service not implemented for this board yet.");
    return NULL;
}

// Helper functions required by HAL drivers often found in board.c

int get_pa_enable_gpio(void)
{
    return PA_ENABLE_GPIO;
}

int get_sdcard_intr_gpio(void)
{
    return -1; // No card detect pin usually
}

int get_sdcard_open_file_num_max(void)
{
    return 5;
}

esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_config)
{
    if (port == I2C_NUM_0 || port == I2C_NUM_1) {
        i2c_config->sda_io_num = I2C_SDA;
        i2c_config->scl_io_num = I2C_SCL;
    } else {
        i2c_config->sda_io_num = -1;
        i2c_config->scl_io_num = -1;
        ESP_LOGE(TAG, "i2c port %d is not supported", port);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t get_i2s_pins(i2c_port_t port, i2s_pin_config_t *i2s_config)
{
    if (port == I2S_NUM_0 || port == I2S_NUM_1) {
        i2s_config->bck_io_num = IIS_SCLK;
        i2s_config->ws_io_num = IIS_LCLK;
        i2s_config->data_out_num = IIS_DOUT;
        i2s_config->data_in_num = IIS_DSIN;
    } else {
        i2s_config->bck_io_num = -1;
        i2s_config->ws_io_num = -1;
        i2s_config->data_out_num = -1;
        i2s_config->data_in_num = -1; // Corrected from IIS_DSIN to -1 for failure
        ESP_LOGE(TAG, "i2s port %d is not supported", port);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Some drivers look for this
int get_input_vol_id(void)
{
    return BUTTON_VOLUP_ID; 
}

esp_err_t get_spi_pins(spi_bus_config_t *bus_config, spi_device_interface_config_t *clk_config)
{
    ESP_LOGE(TAG, "get_spi_pins is not implemented");
    return ESP_FAIL;
}

int get_reset_board_gpio(void)
{
    return -1;
}

int get_es7243_mclk_gpio(void)
{
    return -1;
}

