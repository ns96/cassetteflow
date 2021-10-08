//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_err.h>
#include <esp_log.h>
#include <sdcard_list.h>
#include <sdcard_scan.h>
#include <mbedtls/md.h>
#include <string.h>
#include "mp3db.h"
#include "internal.h"

#define SDCARD_FILE_PREV_NAME           "file:/"

static const char *TAG = "cf_mp3db";

/**
 * get10CharacterHash()
 * @param filename input filename string
 * @param id10c output data (11 bytes size)
 */
static void mp3db_filename_to_id10c(const char *filename, char *id10c)
{
    uint8_t shaResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    const size_t payloadLength = strlen(filename);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char *)filename, payloadLength);
    mbedtls_md_finish(&ctx, shaResult);
    mbedtls_md_free(&ctx);

    // first 10 characters of hex representation (5 bytes)
    sprintf(id10c, "%02x%02x%02x%02x%02x",
            shaResult[0], shaResult[1], shaResult[2], shaResult[3], shaResult[4]);
}

/**
 * Save file to the DB
 * @param filepath full path with filename (/sdcard/file.mp3)
 * @return
 */
static esp_err_t mp3db_file_save(const char *filepath)
{
    FILE *fd_db;

    fd_db = fopen(FILE_MP3DB, "a");
    if (!fd_db) {
        ESP_LOGE(TAG, "Failed to open DB : %s", FILE_MP3DB);
        return ESP_FAIL;
    }

    int duration = 10;  // FIXME
    const char *filename = filepath + strlen("/sdcard/");
    char mp3id[11];
    mp3db_filename_to_id10c(filename, mp3id);

    fprintf(fd_db, "%s\t%d\t%s\n", mp3id, duration, filepath);

    fclose(fd_db);
    return ESP_OK;
}

/**
 * Check if file present in the DB
 * @param filepath full path with filename (/sdcard/file.mp3)
 * @return true if file is already in the db
 */
static bool mp3db_file_exists(const char *filepath)
{
    FILE *fd_db;
    char *line_file;
    bool found = false;

    line_file = malloc(MP3DB_MAX_LINE_LENGTH);
    if (line_file == NULL) {
        return false;
    }

    fd_db = fopen(FILE_MP3DB, "r");
    if (!fd_db) {
        ESP_LOGE(TAG, "Failed to open DB : %s", FILE_MP3DB);
        free(line_file);
        return false;
    }

    // read DB line by line
    while (fscanf(fd_db, "%*s\t%*d\t%1024[^\n]\n", line_file) == 1) {
        if (strcmp(filepath, line_file) == 0) {
            // file present in the DB
            found = true;
            break;
        }
    }

    free(line_file);
    fclose(fd_db);

    return found;
}

/**
 * Get file info from the DB
 * @param mp3id input mp3id (10 characters hash)
 * @param filepath output full path with filename (/sdcard/file.mp3) (at least SDCARD_FILE_PREV_NAME-1 bytes)
 * @param duration output duration in seconds
 * @return true if file is already in the db
 */
esp_err_t mp3db_file_for_id(const char *mp3id, char *filepath, int *duration)
{
    FILE *fd_db;
    char line_mp3id[11];
    char *line_file;
    int line_duration = 0;

    line_file = malloc(MP3DB_MAX_LINE_LENGTH);
    if (line_file == NULL) {
        return ESP_FAIL;
    }

    fd_db = fopen(FILE_MP3DB, "r");
    if (!fd_db) {
        ESP_LOGE(TAG, "Failed to open DB : %s", FILE_MP3DB);
        free(line_file);
        return ESP_FAIL;
    }

    // read DB line by line
    while (fscanf(fd_db, "%10s\t%d\t%1024[^\n]\n", line_mp3id, &line_duration, line_file) == 3) {
        if (strcmp(mp3id, line_mp3id) == 0) {
            // file present in the DB
            strcpy(filepath, line_file);
            *duration = line_duration;
            break;
        }
    }

    free(line_file);
    fclose(fd_db);

    return ESP_OK;
}

static void mp3db_sdcard_url_save_cb(void *user_data, char *url)
{
    if (url == NULL) {
        return;
    }
    if (strlen(url) <= strlen(SDCARD_FILE_PREV_NAME) + 1) {
        return;
    }

    const char *filepath = url + strlen(SDCARD_FILE_PREV_NAME);

    if (mp3db_file_exists(filepath)) {
        ESP_LOGI(TAG, "File already in mp3db: %s", filepath);
        return;
    }

    if (mp3db_file_save(filepath) == ESP_OK) {
        ESP_LOGI(TAG, "Added to mp3db: %s", filepath);
    }
}

// scan for new mp3 files on the SD card
esp_err_t mp3db_scan(void)
{
    // scan for mp3 files
    sdcard_scan(mp3db_sdcard_url_save_cb, "/sdcard", 0, (const char *[]){"mp3"},
                1, NULL);

    ESP_LOGI(TAG, "mp3db_scan done");

    return ESP_OK;
}

esp_err_t mp3db_stop(void)
{
    // nothing for now

    return ESP_OK;
}
