//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#include "bt.h"
#include <string.h>
#include <audio_pipeline.h>
#include <esp_log.h>
#include "esp_peripherals.h"
#include "esp_gap_bt_api.h"
//bt
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_a2dp_api.h"
#include "a2dp_stream.h"

static const char *TAG = "cf_bt";

//bt config
extern bool output_is_bt;
static esp_periph_handle_t bt_periph = NULL;

#define BT_CONNECT_TIMEOUT 20000
#define CONFIG_BT_DEVICE_NAME "Cassetteflow" //device name here
#define CONFIG_BT_REMOTE_NAME "Cassetteflow"
#define CONFIG_BT_PIN {'1', '2', '3', '4'} //pin code here
typedef uint8_t esp_peer_bdname_t[ESP_BT_GAP_MAX_BDNAME_LEN + 1];

static esp_peer_bdname_t remote_bt_device_name;
static esp_bd_addr_t remote_bd_addr = {0};
static bool device_found = false;

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir) {
        return false;
    }

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

static void filter_inquiry_scan_result(esp_bt_gap_cb_param_t *param)
{
    char bda_str[18];
    uint32_t cod = 0;
    int32_t rssi = -129; /* invalid value */
    uint8_t *eir = NULL;
    esp_peer_bdname_t peer_bdname;
    esp_bt_gap_dev_prop_t *p;

    ESP_LOGI(TAG, "Scanned device: %s", bda2str(param->disc_res.bda, bda_str, 18));
    for (int i = 0; i < param->disc_res.num_prop; i++) {
        p = param->disc_res.prop + i;
        switch (p->type) {
            case ESP_BT_GAP_DEV_PROP_COD:
                cod = *(uint32_t *)(p->val);
                ESP_LOGI(TAG, "--Class of Device: 0x%x", cod);
                break;
            case ESP_BT_GAP_DEV_PROP_RSSI:
                rssi = *(int8_t *)(p->val);
                ESP_LOGI(TAG, "--RSSI: %d", rssi);
                break;
            case ESP_BT_GAP_DEV_PROP_EIR:
                eir = (uint8_t *)(p->val);
                get_name_from_eir(eir, (uint8_t *)&peer_bdname, NULL);
                ESP_LOGI(TAG, "--Name: %s", peer_bdname);
                break;
            case ESP_BT_GAP_DEV_PROP_BDNAME:
            default:
                break;
        }
    }

    ESP_LOGI(TAG, "need device name %s", (uint8_t *)remote_bt_device_name);
    /* search for device named "peer_bdname" in its extended inquiry response */
    if (eir) {
        get_name_from_eir(eir, (uint8_t *)&peer_bdname, NULL);
        if (strcmp((char *)peer_bdname, (char *)remote_bt_device_name) != 0) {
            return;
        }

        ESP_LOGI(TAG, "Found a target device, address %s, name %s", bda_str, (uint8_t *)peer_bdname);
        device_found = true;
        memcpy(&remote_bd_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);
        ESP_LOGI(TAG, "Cancel device discovery ...");
        esp_bt_gap_cancel_discovery();
    }
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            filter_inquiry_scan_result(param);
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                if (device_found) {
                    ESP_LOGI(TAG, "Device discovery stopped.");
                    ESP_LOGI(TAG, "a2dp connecting to peer: %s", remote_bt_device_name);
                    device_found = false;
                    esp_a2d_source_connect(remote_bd_addr);
                } else {
                    // not discovered, continue to discover
                    ESP_LOGI(TAG, "Device discovery failed, continue to discover...");
                    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
                }
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(TAG, "Discovery started.");
            }
            break;
        }
        case ESP_BT_GAP_PIN_REQ_EVT: {
            ESP_LOGI(TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
            if (param->pin_req.min_16_digit) {
                ESP_LOGI(TAG, "Input pin code: 0000 0000 0000 0000");
                esp_bt_pin_code_t pin_code = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
            } else {
                esp_bt_pin_code_t pin_code = CONFIG_BT_PIN;
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;
        }
        default:
            break;
    }
}

esp_err_t bt_periph_init(audio_event_iface_handle_t evt)
{
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[-] Start bt peripheral");
    esp_periph_start(set, bt_periph);

    ESP_LOGI(TAG, "[-] Listening event from peripherals (BT)");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);
    return ESP_OK;
}

esp_err_t bt_init_output_stream(audio_element_handle_t *stream_writer)
{
    ESP_LOGI(TAG, "[3.3] Create Bluetooth stream");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_bt_dev_set_device_name(CONFIG_BT_DEVICE_NAME);
    esp_bt_gap_set_pin(pin_type, 4, pin_code);
    esp_bt_gap_register_callback(bt_app_gap_cb);

    const char *remote_name = NULL;
    remote_name = CONFIG_BT_REMOTE_NAME;
    if (remote_name) {
        memcpy(&remote_bt_device_name, remote_name, strlen(remote_name) + 1);
    } else {
        memcpy(&remote_bt_device_name, "ESP_SINK_STREAM_DEMO", ESP_BT_GAP_MAX_BDNAME_LEN);
    }

    a2dp_stream_config_t a2dp_config = {
        .type = AUDIO_STREAM_WRITER,
        .user_callback = {0},
    };
    *stream_writer = a2dp_stream_init(&a2dp_config);
    if (*stream_writer == NULL) {
        ESP_LOGE(TAG, "error init BT stream");
        return ESP_FAIL;
    }

    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0))
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
#else
    esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
#endif

    ESP_LOGI(TAG, "[-] Create Bluetooth peripheral");
    bt_periph = bt_create_periph();
    if (bt_periph == NULL) {
        ESP_LOGE(TAG, "error init bt_periph");
        return ESP_FAIL;
    }
    return ESP_OK;
}

int bt_process_events(audio_event_iface_msg_t msg)
{
    /* Stop when the Bluetooth is disconnected or suspended */
    if (msg.source_type == PERIPH_ID_BLUETOOTH && msg.source == (void *)bt_periph) {
        if ((msg.cmd == PERIPH_BLUETOOTH_DISCONNECTED) || (msg.cmd == PERIPH_BLUETOOTH_AUDIO_SUSPENDED)) {
            ESP_LOGW(TAG, "[ * ] Bluetooth disconnected or suspended");
            periph_bt_stop(bt_periph);
            return 1;
        }
    }
    return 0;
}