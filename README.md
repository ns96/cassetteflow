# Cassette Flow Firmware

This project is a custom firmware for ESP32-based audio boards, originally designed for the **ESP32-LyraT v4.3** and ported to the **Ai-Thinker ESP32-A1S-AudioKit**.

## Supported Boards

*   **Ai-Thinker ESP32-A1S-AudioKit** (ES8388 Codec Version) - *Default Configuration*
*   **ESP32-LyraT v4.3**

## Building for Ai-Thinker ESP32-A1S-AudioKit

The project is currently configured by default for the Ai-Thinker board using a custom component in `components/audio_board`.

### Prerequisites
*   [ESP-IDF v4.3+](https://docs.espressif.com/projects/esp-idf/en/v4.3.2/esp32/get-started/index.html) (v4.3.2 is verified)
*   [ESP-ADF](https://docs.espressif.com/projects/esp-adf/en/latest/get-started/index.html)

### Build Steps
1.  Navigate to the project directory.
2.  Clean the build (optional but recommended):
    ```bash
    idf.py fullclean
    ```
3.  Build the project:
    ```bash
    idf.py build
    ```
4.  Flash and Monitor:
    ```bash
    idf.py -p <PORT> flash monitor
    ```

### Key Board Modifications
*   **MCLK Generation**: MCLK is force-enabled on **GPIO 0** within `board.c` initializtion to support the ES8388 codec.
*   **SD Card**: Configured for 1-Line mode (`SD_MODE_1_LINE`) to match the hardware connections.
*   **LEDs**: The standard display service for LEDs is stubbed out as this board uses simple GPIOs.

---

## Building for ESP32-LyraT v4.3

To target the original LyraT board, you must disable the custom board component and revert to the standard ADF definition.

### Steps to Switch
1.  **Disable Custom Component**: Rename or remove the local `components/audio_board` directory so the build system doesn't find it.
    ```bash
    mv components/audio_board components/audio_board_disabled
    ```
2.  **Configure Menuconfig**:
    *   Run `idf.py menuconfig`
    *   Go to **Audio HAL > Audio Board**
    *   Select **ESP32-LyraT V4.3**
    *   Save and Exit.
3.  **Clean and Build**:
    ```bash
    idf.py fullclean
    idf.py build
    ```

## Troubleshooting

*   **No Audio (Ai-Thinker)**: Ensure GPIO 0 is not being used by other peripherals, as it is required for the MCLK signal to the codec.
*   **SD Card Mount Failed**: Ensure the card is formatted FAT32. The A1S board only supports 1-Line mode securely on most revisions.

## Hardware Buttons

The buttons on the Ai-Thinker AudioKit are mapped as follows:

| Button | Label on Board | Function |
| :--- | :--- | :--- |
| **REC** | KEY1 (GPIO 36) | **Select Side A** (Switch pipeline to use Side A tape file) |
| **MODE** | KEY2 (GPIO 13) | **Select Side B** (Switch pipeline to use Side B tape file) |
| **PLAY** | KEY3 (GPIO 19) | **Play / Passthrough Toggle**<br>- In Decode/Play mode: Switch to Passthrough (Live Input)<br>- In Passthrough: Switch back to Playback<br>- In Encode mode: Start/Stop Recording |
| **SET** | KEY4 (GPIO 23) | **EQ Toggle** (Cycle Equalizer presets) |
| **VOL+** | KEY5 (GPIO 18) | **Volume Up** |
| **VOL-** | KEY6 (GPIO 5)  | **Volume Down** |

## WiFi Configuration

The device connects to WiFi using credentials loaded from the SD card.

1.  Create a file named `wifi_config.txt` in the root of your SD card.
2.  Add your SSID and Password separated by a **tab** character.
    ```
    YourSSID	YourPassword
    ```
3.  If this file is missing, the firmware uses fallback credentials.

To set your own default credentials (if you prefer not to use the SD card), edit `main/config.h` and recompile:
   ```c
   #define CONFIG_WIFI_SSID "YourSSID"
   #define CONFIG_WIFI_PASSWORD "YourPassword"
   ```

To access the web interface or API, find the IP address via the serial monitor (`idf.py monitor`).

## Web API Reference

The board runs an HTTP server on port 80.

| URI | Method | Description | Parameters |
| :--- | :--- | :--- | :--- |
| `/` | GET | Set operation mode | `mode`: `encode`, `decode`, or `pass` |
| `/play` | GET | Start playback | `side`: `a` or `b` |
| `/stop` | GET | Stop pipeline | None |
| `/vol` | GET | Set volume | `value`: 0-100 |
| `/output` | GET | Set audio output | `device`: `SP` (Speaker) or `BT` (Bluetooth)<br>Optional `btdevice`: Name of BT device |
| `/eq` | GET | Set Equalizer | `band`: comma-separated list of 10 integer values |
| `/mp3db` | GET | List MP3 database | None |
| `/tapedb` | GET | List Tape database | None |
| `/info` | GET | Get status info | None |
| `/raw` | GET | Stream raw data | None |
| `/dct` | GET | Enable DCT mapping | Optional `offset`: integer seconds |
| `/create` | GET | Create tape config | `side` (a/b), `tape` (length), `mute`, `data` |
| `/start` | GET | Start encoding | `side`: `a` or `b` |

### Examples

**General Control**
*   **Set Mode to Encode**: `http://<IP>/?mode=encode`
*   **Set Mode to Decode (Play)**: `http://<IP>/?mode=decode`
*   **Set Mode to Passthrough**: `http://<IP>/?mode=pass`
*   **Stop All Operations**: `http://<IP>/stop`
*   **Get System Info**: `http://<IP>/info`
*   **Set Volume to 80%**: `http://<IP>/vol?value=80`

**Playback**
*   **Play Side A**: `http://<IP>/play?side=a`
*   **Play Side B**: `http://<IP>/play?side=b`
*   **Switch Output to Bluetooth**: `http://<IP>/output?device=BT&btdevice=MySpeaker`
*   **Switch Output to Speaker**: `http://<IP>/output?device=SP`
*   **Set Equalizer**: `http://<IP>/eq?band=-2,0,2,4,2,0,-2,-4,-2,0` (10 bands)

**Tape & Database**
*   **List MP3 Database**: `http://<IP>/mp3db`
*   **List Tape Database**: `http://<IP>/tapedb`
*   **Create Tape Config**: `http://<IP>/create?side=a&tape=60&mute=5&data=0001,mp3_1,mp3_2...`
*   **Start Encoding Side A**: `http://<IP>/start?side=a`

**Data Streaming**
*   **Stream Raw Line Data**: `http://<IP>/raw`
*   **Enable DCT Mapping**: `http://<IP>/dct`
*   **Enable DCT Mapping with Offset**: `http://<IP>/dct?offset=1800` (Shift mapping by 30 mins)

## Monitoring CPU Usage

To view real-time CPU usage statistics per task on the serial monitor:

1.  Run `idf.py menuconfig`.
2.  Navigate to **Component config** -> **FreeRTOS**.
3.  Enable **Enable FreeRTOS to collect run time stats**.
4.  Save and Exit.
5.  Rebuild and flash: `idf.py build flash monitor`.

The console will output a table every second showing task runtimes and CPU percentage.

