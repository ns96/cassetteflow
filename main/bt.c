//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#include "bt.h"
#include <string.h>
#include <esp_log.h>
#include "esp_peripherals.h"
#include "esp_gap_bt_api.h"
//bt
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_a2dp_api.h"
#include "a2dp_stream.h"
#include "raw_queue.h"
#include "pipeline_output.h"


#define CONFIG_BT_DEVICE_NAME "Cassetteflow" //device name here
#define MAX_DEVICES_IN_LIST         (20)
static const char *TAG = "cf_bt";

extern audio_event_iface_handle_t evt;
extern esp_periph_set_handle_t set;

typedef uint8_t esp_peer_bdname_t[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
//bt config
extern bool output_is_bt;
static esp_periph_handle_t bt_periph = NULL;
static esp_peer_bdname_t remote_bt_device_name;
static esp_bd_addr_t remote_bd_addr = {0};
static bool device_found = false;

static int bt_scan_mode = BT_SCAN_MODE;
static bool bt_is_init = false;
static bool bt_is_enabled = false;

static raw_queue_message_t msg;

static char **list = NULL;
static int devices_in_list = 0;

static void init_devices_list(void)
{
    devices_in_list = 0;
    list = malloc(MAX_DEVICES_IN_LIST * sizeof(char *));
    for (int i = 0; i < MAX_DEVICES_IN_LIST; ++i) {
        list[i] = NULL;
    }
}

static void deinit_devices_list(void)
{
    for (int i = 0; i < devices_in_list; ++i) {
        if (list[i] != NULL) {
            free(list[i]);
        }
    }
    if (list != NULL) {
        free(list);
    }
}

static int search_device_in_list(const char *name)
{
    for (int i = 0; i < devices_in_list; ++i) {
        if (strcmp(list[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_device_to_list(const char *name)
{
    if (devices_in_list >= MAX_DEVICES_IN_LIST) {
        ESP_LOGI(TAG, "Can`t store device name, list limit reached: %d", MAX_DEVICES_IN_LIST);
        return;
    }
    list[devices_in_list] = (char *)malloc(ESP_BT_GAP_MAX_BDNAME_LEN + 1);
    strcpy(list[devices_in_list], name);
    devices_in_list++;
}

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

    if (!eir) {
        return;
    }

    get_name_from_eir(eir, (uint8_t *)&peer_bdname, NULL);
    if (strcmp((char *)peer_bdname, (char *)remote_bt_device_name) == 0) {
        device_found = true;
    }

    ESP_LOGI(TAG, "Found device, address %s, name %s", bda_str, (uint8_t *)peer_bdname);
    switch (bt_scan_mode) {
        case BT_SCAN_MODE:
            //stop discovery when found device already saved in list
            if(search_device_in_list((char *)&peer_bdname)) {
                //ESP_LOGI(TAG, "Device already in list. Stopping discovery");
                //esp_bt_gap_cancel_discovery();
            } else {
                ESP_LOGI(TAG, "Add device %s to list", (char *)&peer_bdname);
                add_device_to_list((char *)&peer_bdname);
                size_t device_name_length = strlen((char *)&peer_bdname);
                if (device_name_length > sizeof(msg.line)) {
                    device_name_length = sizeof(msg.line);
                }
                memcpy(msg.line, (char *)&peer_bdname, device_name_length + 1);

                if (device_found) {
                    strncat(msg.line, " *", sizeof(msg.line) - 1);
                    device_found = false;
                }
                raw_queue_send(1, &msg);
            }
            break;

        case BT_CONNECT_MODE:
            if (!device_found) {
                return;
            }
            memcpy(&remote_bd_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);
            ESP_LOGI(TAG, "Device found. Stopping discovery");
            esp_bt_gap_cancel_discovery();
            break;
    }
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (!bt_is_enabled) {
        return;
    }

    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            filter_inquiry_scan_result(param);
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(TAG, "Device discovery stopped");
                deinit_devices_list();
                if (device_found && (bt_scan_mode == BT_CONNECT_MODE)) {
                    ESP_LOGI(TAG, "a2dp connecting to peer: %s", remote_bt_device_name);
                    esp_a2d_source_connect(remote_bd_addr);
                }
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(TAG, "Discovery started.");
                init_devices_list();
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
                esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;
        }
        default:
            ESP_LOGI(TAG, "Unknown event %d", event);
            break;
    }
}

esp_err_t bt_init(void)
{
    if (bt_is_init) {
        return ESP_OK;
    }
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

    bt_is_init = true;
    return ESP_OK;
}

static esp_err_t bt_start_scan(void)
{
    esp_err_t err = ESP_FAIL;

    device_found = false;
    bt_is_enabled = true;

    err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    return err;
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

esp_err_t bt_deinit(void)
{
        bt_is_enabled = false;
        //bt_is_init = false;

    if (bt_periph) {
        periph_bt_stop(bt_periph);
        esp_avrc_ct_deinit();
        esp_avrc_tg_deinit();
        a2dp_destroy();
        esp_periph_stop(bt_periph);
        esp_periph_destroy(bt_periph);

        //audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);
        //disabling bluetooth stop wifi working!
        //ESP_ERROR_CHECK(esp_bluedroid_disable());
        //ESP_ERROR_CHECK(esp_bluedroid_deinit());
        //ESP_ERROR_CHECK(esp_bt_controller_disable());
        //ESP_ERROR_CHECK(esp_bt_controller_deinit());
        bt_periph = NULL;
    }
    vTaskDelay(100);
    return ESP_OK;
}

void bt_pause(void)
{
    if (bt_periph) {
        periph_bt_pause(bt_periph);
    }
}

void bt_play(void)
{
    if (bt_periph) {
        periph_bt_play(bt_periph);
    }
}

void bt_set_device(const char *device, size_t device_len)
{
    if (device) {
        memcpy(remote_bt_device_name, device, device_len);
    }
    if (bt_is_init) {
        bt_deinit();
    }
}

esp_err_t bt_connect_device(void)
{
    bt_scan_mode = BT_CONNECT_MODE;

    bt_start_scan();

    ESP_LOGI(TAG, "[-] Create Bluetooth peripheral");
    if (!bt_periph) {
        bt_periph = bt_create_periph();
        if (bt_periph == NULL) {
            ESP_LOGE(TAG, "[-] Error init bt_periph");
            return ESP_FAIL;
        }
        // Initialize peripherals management
        ESP_LOGI(TAG, "[-] Start bt peripheral");
        esp_periph_start(set, bt_periph);
    }

    //ESP_LOGI(TAG, "[-] Listening event from peripherals");
    //audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    return ESP_OK;
}

esp_err_t bt_get_devices_list(void)
{
    bt_scan_mode = BT_SCAN_MODE;

    if (bt_init() != ESP_OK) {
        return ESP_FAIL;
    }

    if (bt_start_scan() != ESP_OK) {
        return ESP_FAIL;
    }

    strcpy(msg.line, "Devices list :");
    raw_queue_send(1, &msg);

    return ESP_OK;
}