#ifndef _BOARD_DEF_H_CUSTOM_
#define _BOARD_DEF_H_CUSTOM_

#include "driver/gpio.h"

// Ai-Thinker AudioKit v2.2 (ES8388) Pinout

// I2S (Audio Data)
#define AUDIO_CODEC_DEFAULT_CONFIG(){                   \
        .adc_input  = AUDIO_HAL_ADC_INPUT_LINE1,        \
        .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,         \
        .codec_mode = AUDIO_HAL_CODEC_MODE_BOTH,        \
        .i2s_iface = {                                  \
            .mode = AUDIO_HAL_MODE_SLAVE,               \
            .fmt = AUDIO_HAL_I2S_NORMAL,                \
            .samples = AUDIO_HAL_44K_SAMPLES,           \
            .bits = AUDIO_HAL_BIT_LENGTH_16BITS,        \
        },                                              \
}

#define IIS_SCLK                    27
#define IIS_LCLK                    25
#define IIS_DSIN                    35
#define IIS_DOUT                    26
#define IIS_MCLK                    0

// I2C (Control)
#define I2C_NUM                     0
#define I2C_SDA                     33
#define I2C_SCL                     32

// Power Amplifier
#define PA_ENABLE_GPIO              21

// SD Card (Standard SDMMC Slot 1)
// CLK=14, CMD=15, D0=2 (Defined by Driver/Slot, usually automatic if using SDMMC Host)
// But we might need to define detect pin if present. A1S usually doesn't have a specific CD pin wired or uses a GPIO.
// We'll leave it to the SD card peripheral default.

// Keys
// AudioKit v2.2 keys are physically on:
// KEY1: GPIO 36
// KEY2: GPIO 13
// KEY3: GPIO 19
// KEY4: GPIO 23
// KEY5: GPIO 18
// KEY6: GPIO 5

// We map them to ADF input key definitions.
// CassetteFlow uses keys.c to read these. We must provide the correct configuration in board.c

#define BUTTON_REC_ID              GPIO_NUM_36
#define BUTTON_MODE_ID             GPIO_NUM_13
#define BUTTON_PLAY_ID             GPIO_NUM_19
#define BUTTON_SET_ID              GPIO_NUM_23
#define BUTTON_VOLUP_ID            GPIO_NUM_18
#define BUTTON_VOLDOWN_ID          GPIO_NUM_5

#define INPUT_KEY_NUM     6

#define INPUT_KEY_DEFAULT_INFO() { \
    { \
        .type = PERIPH_ID_BUTTON, \
        .user_id = INPUT_KEY_USER_ID_REC, \
        .act_id = BUTTON_REC_ID, \
    }, \
    { \
        .type = PERIPH_ID_BUTTON, \
        .user_id = INPUT_KEY_USER_ID_MODE, \
        .act_id = BUTTON_MODE_ID, \
    }, \
    { \
        .type = PERIPH_ID_BUTTON, \
        .user_id = INPUT_KEY_USER_ID_PLAY, \
        .act_id = BUTTON_PLAY_ID, \
    }, \
    { \
        .type = PERIPH_ID_BUTTON, \
        .user_id = INPUT_KEY_USER_ID_SET, \
        .act_id = BUTTON_SET_ID, \
    }, \
    { \
        .type = PERIPH_ID_BUTTON, \
        .user_id = INPUT_KEY_USER_ID_VOLUP, \
        .act_id = BUTTON_VOLUP_ID, \
    }, \
    { \
        .type = PERIPH_ID_BUTTON, \
        .user_id = INPUT_KEY_USER_ID_VOLDOWN, \
        .act_id = BUTTON_VOLDOWN_ID, \
    }, \
}

// LEDs
// A1S v2.2 LEDs:
// D4 (Green) = GPIO 22
// D5 (Red)   = GPIO 19 (Shared with Key3?! Check schematic. Often LEDs are 22 and 19)
// Actually on A1S:
// GPIO 22 is often LED D4.
// GPIO 19 is often LED D5 OR Key. On V2.2, 19 is KEY3.
// Let's use GPIO 22 as the primary LED.
#define LED_GREEN_GPIO             22

// Auxiliary Inputs
// ES8388 has LINPUT1/RINPUT1 and LINPUT2/RINPUT2.
// Board def says LINE1.
#define AUDIO_CODEC_ES8388_ADDR     0x20 // Or detection

#endif
