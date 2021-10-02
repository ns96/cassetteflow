//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_event.h>
#include <esp_log.h>
#include "esp_netif.h"
#include "esp_eth.h"
#include <esp_http_server.h>

static const char *TAG = "cf_http_server";

static httpd_handle_t httpd_server = NULL;

/* sets the board in either encode or decode (default) mode. */
static esp_err_t handler_uri_root(httpd_req_t *req)
{
    size_t buf_len;

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
                // TODO handle param
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
    // TODO
    return ESP_OK;
}

// returns the tape db containing unique tape Ids and their associated mp3 ids
static esp_err_t handler_uri_tapedb(httpd_req_t *req)
{
    // TODO
    return ESP_OK;
}

static esp_err_t handler_uri_info(httpd_req_t *req)
{
    // TODO
    return ESP_OK;
}

static esp_err_t handler_uri_raw(httpd_req_t *req)
{
    // TODO
    return ESP_OK;
}

// http://lyra.board.ip/create?side=[a,b]&tape=[60,90,110,120]&data=”tapeId,mp3id_1,mp3id_2,mp3id_3,mp3id_4, ...”
// -- generates the text file for side A or B to be encoded onto a 60, 90, 110, or 120 minute tape
static esp_err_t handler_uri_create(httpd_req_t *req)
{
    // TODO parse query params ( see handler_uri_root)
    // TODO create sideA.txt or sideB.txt in the root folder of SD card
    return ESP_OK;
}

static esp_err_t handler_uri_start(httpd_req_t *req)
{
    // TODO parse query params ( see handler_uri_root)
    // TODO start
    return ESP_OK;
}

static esp_err_t handler_uri_stop(httpd_req_t *req)
{
    // TODO
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

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

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
        httpd_register_uri_handler(server, &uri_create);
        httpd_register_uri_handler(server, &uri_start);
        httpd_register_uri_handler(server, &uri_stop);
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