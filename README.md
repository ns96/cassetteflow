# Cassette Flow

## Initial setup

1. [Setup ESP-IDF](https://docs.espressif.com/projects/esp-adf/en/latest/get-started/index.html#step-1-set-up-esp-idf), use ESP-IDF [v4.3.1](https://github.com/espressif/esp-idf/releases/tag/v4.3.1)
2. [Setup ESP-ADF](https://docs.espressif.com/projects/esp-adf/en/latest/get-started/index.html#step-2-get-esp-adf), use latest master version
3. Patch ESP-ADF to enable audio capture from AUX_IN input. In file `esp-adf/components/audio_board/lyrat_v4_3/board_def.h` change line 49 from:

````
.adc_input  = AUDIO_HAL_ADC_INPUT_LINE1,        \
````
to
````
.adc_input  = AUDIO_HAL_ADC_INPUT_LINE2,        \
````

## Wi-Fi configuration

1. Create wifi_config.txt in the root of the SD card
2. Add one line in the following format: `<WiFIAP>\t<password>` (WiFi AP and password separated with TAB on one line)
- or
1. Open main/config.h
2. Set CONFIG_WIFI_SSID and CONFIG_WIFI_PASSWORD to your Wi-Fi AP

## Develop/Build/Flash project

1. Open terminal in the project's folder and run to set ESP-IDF environment variables: `. $HOME/esp/esp-idf/export.sh`
2. Build project: `idf.py build`
3. Enter upload mode: Manually by pressing both Boot and RST keys and then releasing first RST and then Boot key.
4. Flash project: `idf.py flash`

## Develop/Build project in CLion

1. Open project in CLion
2. Open CLion's terminal and run to set ESP-IDF environment variables: `. $HOME/esp/esp-idf/export.sh`
3. Edit/update
4. Build project: `idf.py build`

## Minimodem test commands

1. Encode: `minimodem --tx 1200 -f wavfile.wav`
2. Decode: `minimodem -r 1200`

## CPU load measurement

Enable the following configuration options (`idf.py menuconfig`) to get CPU load reported to the serial console every 1 second:

```
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=y
CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER=y
```

## Setup CLion

1. https://www.jetbrains.com/help/clion/esp-idf.html#cmake-setup

## ESP32 LyraT 4.3 board
1. AUX_IN audio quality issue - https://esp32.com/viewtopic.php?t=12407