# Cassette Flow

## Initial setup

1. Setup ESP-IDF +
   ESP-ADF: https://docs.espressif.com/projects/esp-adf/en/latest/get-started/index.html#step-1-set-up-esp-idf

## Wi-Fi configuration

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

## Setup CLion

1. https://www.jetbrains.com/help/clion/esp-idf.html#cmake-setup
