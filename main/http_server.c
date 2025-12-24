//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_event.h>
#include <esp_log.h>
#include "esp_netif.h"
#include "pipeline.h"
#include "tapefile.h"
#include "raw_queue.h"
#include <esp_http_server.h>
#include "eq.h"
#include "pipeline_output.h"
#include "board.h"
#include "volume.h"
#include "bt.h"
#include <ctype.h>
#include "pipeline_decode.h"

static const char *TAG = "cf_http_server";

/* Scratch buffer size */
#define SCRATCH_BUFSIZE  8192
/* Scratch buffer for temporary storage during file transfer */
static char http_scratch_buffer[SCRATCH_BUFSIZE];

static httpd_handle_t httpd_server = NULL;

static esp_err_t read_param_from_req(httpd_req_t *req, const char *param_name, char *output, size_t output_length)
{
    size_t buf_len;
    esp_err_t err = ESP_FAIL;

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf;
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, param_name, output, output_length) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => %s=%s", param_name, output);
                err = ESP_OK;
            }
        }
        free(buf);
    }
    return err;
}

/**
 * Decode URL string.
 * https://github.com/swarajsatvaya/UrlDecoder-C
 * @param str source string
 * @return Allocated in memory string. Must be free after use
 */
static char *urlDecode(const char *str) {
    //int d = 0; /* whether or not the string is decoded */

    char *dStr = (char *) malloc(strlen(str) + 1);
    char eStr[] = "00"; /* for a hex code */

    strcpy(dStr, str);

    //while(!d) {
    //d = 1;
    int i; /* the counter for the string */

    for(i=0;i<strlen(dStr);++i) {

        if(dStr[i] == '%') {
            if(dStr[i+1] == 0)
                return dStr;

            if(isxdigit(dStr[i+1]) && isxdigit(dStr[i+2])) {

                //d = 0;

                /* combine the next to numbers into one */
                eStr[0] = dStr[i+1];
                eStr[1] = dStr[i+2];

                /* convert it to decimal */
                long int x = strtol(eStr, NULL, 16);

                /* remove the hex */
                memmove(&dStr[i+1], &dStr[i+3], strlen(&dStr[i+3])+1);

                dStr[i] = x;
            }
        }
        else if(dStr[i] == '+') { dStr[i] = ' '; }
    }
    //}
    return dStr;
}

static esp_err_t http_respond_file(httpd_req_t *req, const char *filepath)
{
    FILE *fd = NULL;
    struct stat file_stat;

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filepath, file_stat.st_size);

    httpd_resp_set_type(req, "text/plain");

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = http_scratch_buffer;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }

        /* Keep looping till the whole file is sent */
    }
    while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

/* sets the board in either encode or decode (default) mode. */
static esp_err_t handler_uri_root(httpd_req_t *req)
{
    size_t buf_len;

    ESP_LOGI(TAG, "%s", __FUNCTION__);

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf;
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "mode", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => mode=%s", param);
                //check mode param
                if (strncmp(param, "encode", strlen("encode")) == 0) {
                    pipeline_set_mode(MODE_ENCODE);
                } else if (strncmp(param, "decode", strlen("decode")) == 0) {
                    pipeline_set_mode(MODE_DECODE);
                } else if (strncmp(param, "pass", strlen("pass")) == 0) {
                    pipeline_set_mode(MODE_PASSTHROUGH);
                }
            }
        }
        free(buf);
    }

    /* Respond with empty body */
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

// returns the mp3 database as a tab delimited text file (see format)
static esp_err_t handler_uri_mp3db(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    http_respond_file(req, FILE_AUDIODB);
    return ESP_OK;
}

// returns the tape db containing unique tape Ids and their associated mp3 ids
static esp_err_t handler_uri_tapedb(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    http_respond_file(req, FILE_TAPEDB);
    return ESP_OK;
}

static esp_err_t handler_uri_info(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    char str[256];

    pipeline_current_info_str(str, sizeof(str));

    httpd_resp_sendstr(req, str);
    return ESP_OK;
}

/**
 * continuously streams the raw decoded line records to the client.
 * @param req
 * @return
 */
static esp_err_t handler_uri_raw(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    esp_err_t ret = ESP_OK;

    httpd_resp_set_type(req, "text/plain");

    raw_queue_reset(0);
    pipeline_decode_set_dct_mapping(false, 0);

    while (ret == ESP_OK) {
        // read current line (up to 10 seconds)
        raw_queue_message_t msg;
        ret = raw_queue_get(0, &msg, pdMS_TO_TICKS(10 * 1000));
        if (ret == ESP_OK) {
            // send one line at a time
            strncat(msg.line, "\n", sizeof(msg.line) - 1);
            ret = httpd_resp_send_chunk(req, msg.line, HTTPD_RESP_USE_STRLEN);
        }
    }

    /* End of transmission */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/**
 * continuously streams the raw decoded line records to the client with DCT mapping enabled.
 * @param req
 * @return
 */
static esp_err_t handler_uri_dct(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    esp_err_t ret = ESP_OK;

    httpd_resp_set_type(req, "text/plain");

    int offset = 0;
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "offset", param, sizeof(param)) == ESP_OK) {
                offset = atoi(param);
                ESP_LOGI(TAG, "Found URL query parameter => offset=%d", offset);
            }
        }
        free(buf);
    }

    raw_queue_reset(0);
    pipeline_decode_set_dct_mapping(true, offset);

    while (ret == ESP_OK) {
        // read current line (up to 10 seconds)
        raw_queue_message_t msg;
        ret = raw_queue_get(0, &msg, pdMS_TO_TICKS(10 * 1000));
        if (ret == ESP_OK) {
            // send one line at a time
            strncat(msg.line, "\n", sizeof(msg.line) - 1);
            ret = httpd_resp_send_chunk(req, msg.line, HTTPD_RESP_USE_STRLEN);
        }
    }

    /* End of transmission */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// http://lyra.board.ip/create?side=[a,b]&tape=[60,90,110,120]&data=”tapeId,mp3id_1,mp3id_2,mp3id_3,mp3id_4, ...”
// -- generates the text file for side A or B to be encoded onto a 60, 90, 110, or 120 minute tape
static esp_err_t handler_uri_create(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    size_t buf_len;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    char param_side[8] = "a";
    char param_tape[128] = "60";
    char param_mute[128] = "0";
    char *param_data = NULL;

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf;
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "side", param_side, sizeof(param_side)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => side=%s", param_side);
            }
            if (httpd_query_key_value(buf, "tape", param_tape, sizeof(param_tape)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => tape=%s", param_tape);
            }
            if (httpd_query_key_value(buf, "mute", param_mute, sizeof(param_mute)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => mute=%s", param_mute);
            }
            param_data = malloc(buf_len);
            if (param_data != NULL) {
                param_data[0] = 0;
                if (httpd_query_key_value(buf, "data", param_data, buf_len) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query parameter => data=%s", param_data);
                }
            }
        }
        free(buf);
    }

    if ((strlen(param_side) == 1) && (strlen(param_tape) > 0)
        && (param_data != NULL) && (strlen(param_data) > 0)
        && (strlen(param_mute) > 0)) {
        int tape_length_minutes = atoi(param_tape);
        int mute_seconds = atoi(param_mute);
        const char side = param_side[0];
        err = tapefile_create(toupper(side), tape_length_minutes, param_data, mute_seconds);
    }
    free(param_data);

    switch (err) {
        case ESP_OK:
            /* Respond with empty body */
            httpd_resp_send(req, NULL, 0);
            break;
        case ESP_ERR_INVALID_SIZE:
            /* Respond with warning message */
            httpd_resp_sendstr(req, "Playback time of the mp3 files would exceed the length of the tape side");
            break;
        case ESP_ERR_INVALID_ARG:
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
            break;
        case ESP_ERR_NOT_FOUND:
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ID not found in the mp3 database");
            break;
        case ESP_FAIL:
        default:
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create txt file");
    }
    return ESP_OK;
}

static esp_err_t handler_uri_start(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    size_t buf_len;
    esp_err_t err = ESP_FAIL;
    char param_side[32] = "";

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf;
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "side", param_side, sizeof(param_side)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => side=%s", param_side);
            }
        }
        free(buf);
    }

    if (strlen(param_side) == 1) {
        err = pipeline_start_encoding(param_side[0]);
    }

    if (err == ESP_OK) {
        /* Respond with empty body */
        httpd_resp_send(req, NULL, 0);
    } else {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start encoding");
    }
    return ESP_OK;
}

static esp_err_t handler_uri_stop(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    esp_err_t err = pipeline_stop();

    if (err == ESP_OK) {
        /* Respond with empty body */
        httpd_resp_send(req, NULL, 0);
    } else {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to stop");
    }
    return ESP_OK;
}

static esp_err_t handler_uri_eq(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    size_t buf_len;
    esp_err_t err = ESP_FAIL;
    char param_eq[32] = "";
    int bands[10] = {0};

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf;
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "band", param_eq, sizeof(param_eq)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => eq band=%s", param_eq);
            }
        }
        free(buf);
    }

    //read and process band params
    if (sscanf(param_eq, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &bands[0], &bands[1], &bands[2], &bands[3], &bands[4],
               &bands[5], &bands[6], &bands[7], &bands[8], &bands[9]) == 10) {
        err = eq_process_bands(bands);
    } else {
        err = ESP_FAIL;
    }

    if (err == ESP_OK) {
        /* Respond with empty body */
        httpd_resp_send(req, NULL, 0);
    } else {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set EQ");
    }
    return ESP_OK;
}

static esp_err_t handler_uri_output(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    size_t buf_len;
    esp_err_t err = ESP_FAIL;
    char param[32] = "";
    char response_buff[255] = {0};
    esp_err_t ret = ESP_OK;

    httpd_resp_set_type(req, "text/plain");

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf;
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) != ESP_OK) {
            goto exit;
        }
        ESP_LOGI(TAG, "Found URL query => %s", buf);
        /* Get value of expected key from query string */
        if (httpd_query_key_value(buf, "device", param, sizeof(param)) != ESP_OK) {
            snprintf(response_buff, sizeof(response_buff),"Parameter 'device' not found");
            goto exit;
        }

        ESP_LOGI(TAG, "Found URL query parameter => device=%s", param);
        //check device param
        if (strncmp(param, "BT", strlen("BT")) == 0) {

            //if set `btdevice` connect to it
            if (httpd_query_key_value(buf, "btdevice", param, sizeof(param)) == ESP_OK) {
                char *device;
                device = urlDecode(param);
                ESP_LOGI(TAG, "Decoded device name => %s", device);
                err = pipeline_set_output_bt(true, device);
                free(device);
                if (err != ESP_OK){
                    snprintf(response_buff, sizeof(response_buff), "Failed connect to device => %s", param);
                }
            } else {
                //get devices list
                raw_queue_reset(1);
                err = bt_get_devices_list();
                if (err != ESP_OK) {
                    goto exit;
                }
                while (ret == ESP_OK) {
                    // read current line (up to 5 seconds)
                    raw_queue_message_t msg;
                    ret = raw_queue_get(1, &msg, pdMS_TO_TICKS(5 * 1000));
                    if (ret == ESP_OK) {
                        // send one line at a time
                        strncat(msg.line, "\n", sizeof(msg.line) - 1);
                        ret = httpd_resp_send_chunk(req, msg.line, HTTPD_RESP_USE_STRLEN);
                    }
                }
                httpd_resp_send_chunk(req, NULL, 0);
                goto exit;
            }
        } else if (strncmp(param, "SP", strlen("SP")) == 0) {
            err = pipeline_set_output_bt(false, NULL);
            if (err != ESP_OK) {
                snprintf(response_buff, sizeof(response_buff),"Failed to set output to SP");
            }
        } else {
            snprintf(response_buff, sizeof(response_buff),"Unknown output device => %s", param);
        }
        exit:        free(buf);
    }
    
    if (err == ESP_OK) {
        /* Respond with body */
        httpd_resp_sendstr(req, response_buff);
    } else {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, response_buff);
    }
    return ESP_OK;
}

static esp_err_t handler_uri_play(httpd_req_t *req)
{
    char param[32] = {0};

    ESP_LOGI(TAG, "%s", __FUNCTION__);

    esp_err_t err = ESP_FAIL;
    err = read_param_from_req(req, "side", param, sizeof(param));
    if (err == ESP_OK) {
        if (strlen(param) == 1) {
            const char side = toupper(param[0]);
            pipeline_start_playing(side);
        } else {
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK) {
        /* Respond with empty body */
        httpd_resp_send(req, NULL, 0);
    } else {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start play");
    }
    return ESP_OK;
}

static esp_err_t handler_uri_vol(httpd_req_t *req)
{
    char param[32] = {0};

    ESP_LOGI(TAG, "%s", __FUNCTION__);

    esp_err_t err = ESP_FAIL;
    err = read_param_from_req(req, "value", param, sizeof(param));
    if (err == ESP_OK) {
        long value = strtol(param, NULL, 10);
        if ((value != LONG_MAX) && (value != LONG_MIN)) {
            err = volume_set((int)value);
        } else {
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK) {
        /* Respond with empty body */
        httpd_resp_send(req, NULL, 0);
    } else {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set volume");
    }
    return ESP_OK;
}

static const httpd_uri_t uri_root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = handler_uri_root,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_mp3db = {
    .uri       = "/mp3db",
    .method    = HTTP_GET,
    .handler   = handler_uri_mp3db,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_tapedb = {
    .uri       = "/tapedb",
    .method    = HTTP_GET,
    .handler   = handler_uri_tapedb,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_info = {
    .uri       = "/info",
    .method    = HTTP_GET,
    .handler   = handler_uri_info,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_raw = {
    .uri       = "/raw",
    .method    = HTTP_GET,
    .handler   = handler_uri_raw,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_dct = {
    .uri       = "/dct",
    .method    = HTTP_GET,
    .handler   = handler_uri_dct,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_create = {
    .uri       = "/create",
    .method    = HTTP_GET,
    .handler   = handler_uri_create,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_start = {
    .uri       = "/start",
    .method    = HTTP_GET,
    .handler   = handler_uri_start,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_stop = {
    .uri       = "/stop",
    .method    = HTTP_GET,
    .handler   = handler_uri_stop,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_eq = {
    .uri       = "/eq",
    .method    = HTTP_GET,
    .handler   = handler_uri_eq,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_output = {
    .uri       = "/output",
    .method    = HTTP_GET,
    .handler   = handler_uri_output,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_play = {
    .uri       = "/play",
    .method    = HTTP_GET,
    .handler   = handler_uri_play,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_vol = {
    .uri       = "/vol",
    .method    = HTTP_GET,
    .handler   = handler_uri_vol,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_mp3db);
        httpd_register_uri_handler(server, &uri_tapedb);
        httpd_register_uri_handler(server, &uri_info);
        httpd_register_uri_handler(server, &uri_raw);
        httpd_register_uri_handler(server, &uri_dct);
        httpd_register_uri_handler(server, &uri_create);
        httpd_register_uri_handler(server, &uri_start);
        httpd_register_uri_handler(server, &uri_stop);
        httpd_register_uri_handler(server, &uri_eq);
        httpd_register_uri_handler(server, &uri_output);
        httpd_register_uri_handler(server, &uri_play);
        httpd_register_uri_handler(server, &uri_vol);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

esp_err_t http_server_start(void)
{
    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &connect_handler, &httpd_server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               &disconnect_handler, &httpd_server));

    /* Start the server for the first time */
    httpd_server = start_webserver();

    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (httpd_server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(httpd_server);
    }

    return ESP_OK;
}