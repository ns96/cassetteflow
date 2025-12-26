//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_err.h>
#include <esp_log.h>
#include <sdcard_list.h>
#include <sdcard_scan.h>
#include <mbedtls/md.h>
#include <string.h>
#include <stdlib.h>
#include "audiodb.h"
#include "internal.h"
#include "mp3info.h"
#include "flacinfo.h"

#define SDCARD_FILE_PREV_NAME           "file:/"

static const char *TAG = "cf_audiodb";

/**
 * get10CharacterHash()
 * @param filename input filename string
 * @param id10c output data (11 bytes size)
 */
static void audiodb_filename_to_id10c(const char *filename, char *id10c)
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
 * @param filepath full path with filename (/sdcard/file.mp3(.flac))
 * @return
 */
static esp_err_t audiodb_file_save(const char *filepath)
{
    FILE *fd_db;
    int duration = 0, avg_bitrate = 0;

    fd_db = fopen(FILE_AUDIODB, "a");
    if (!fd_db) {
        ESP_LOGE(TAG, "Failed to open DB : %s", FILE_AUDIODB);
        return ESP_FAIL;
    }
    if (strcmp((filepath + strlen(filepath) - 3), "mp3") == 0) {
        mp3info_get_info(filepath, &duration, &avg_bitrate);
    } else if (strcmp((filepath + strlen(filepath) - 4), "flac") == 0) {
        flacinfo_get_info(filepath, &duration, &avg_bitrate);
    }
    const char *filename = filepath + strlen("/sdcard/");
    char audioid[11];
    audiodb_filename_to_id10c(filename, audioid);

    fprintf(fd_db, "%s\t%d\t%d\t%s\n", audioid, duration, avg_bitrate, filepath);

    fclose(fd_db);
    return ESP_OK;
}

static bool audiodb_file_exists(const char *filepath)
{
    FILE *fd_db;
    char *line_buf;
    bool found = false;

    line_buf = malloc(AUDIODB_MAX_LINE_LENGTH);
    if (line_buf == NULL) {
        return false;
    }

    fd_db = fopen(FILE_AUDIODB, "r");
    if (!fd_db) {
        ESP_LOGE(TAG, "Failed to open DB : %s", FILE_AUDIODB);
        free(line_buf);
        return false;
    }

    // read DB line by line
    while (fgets(line_buf, AUDIODB_MAX_LINE_LENGTH, fd_db) != NULL) {
        // Parse line: HASH\tDUR\tBIT\tPATH[\tEXTRA]
        // We only care about the PATH column (idx 3)
        // Tokenize? or just iterate tabs?
        // Use manual iteration to handle spaces in filenames correctly (though filenames shouldn't have tabs)

        char *ptr = line_buf;
        int tab_count = 0;
        char *path_start = NULL;
        
        while (*ptr != 0 && *ptr != '\n' && *ptr != '\r') {
            if (*ptr == '\t') {
                tab_count++;
                if (tab_count == 3) {
                    path_start = ptr + 1;
                    // Find end of path (next tab or newline)
                    char *path_end = path_start;
                    while (*path_end != 0 && *path_end != '\t' && *path_end != '\n' && *path_end != '\r') {
                        path_end++;
                    }
                    *path_end = 0; // terminate string
                    break;
                }
            }
            ptr++;
        }

        if (path_start && strcmp(filepath, path_start) == 0) {
            found = true;
            break;
        }
    }

    free(line_buf);
    fclose(fd_db);

    return found;
}

/**
 * Get file info from the DB
 * @param audioid input audioid (10 characters hash)
 * @param filepath output full path with filename (/sdcard/file.mp3(.flac)) (at least SDCARD_FILE_PREV_NAME-1 bytes)
 *  (can be NULL)
 * @param duration output duration in seconds (can be NULL)
 * @param avg_bitrate output average bitrate in bits per second (can be NULL)
 * @return true if file is already in the db
 */
esp_err_t audiodb_file_for_id(const char *audioid, char *filepath, int *duration, int *avg_bitrate)
{
    FILE *fd_db;
    char *line_buf;
    esp_err_t ret = ESP_FAIL;

    line_buf = malloc(AUDIODB_MAX_LINE_LENGTH);
    if (line_buf == NULL) {
        return ESP_FAIL;
    }

    fd_db = fopen(FILE_AUDIODB, "r");
    if (!fd_db) {
        ESP_LOGE(TAG, "Failed to open DB : %s", FILE_AUDIODB);
        free(line_buf);
        return ESP_FAIL;
    }

    // read DB line by line
    while (fgets(line_buf, AUDIODB_MAX_LINE_LENGTH, fd_db) != NULL) {
        // Parse line: HASH\tDUR\tBIT\tPATH[\tEXTRA]
        char *ptr = line_buf;
        
        // 1. Get Hash
        char *line_hash = ptr;
        char *next_tab = strchr(ptr, '\t');
        if (!next_tab) continue;
        *next_tab = 0;
        
        if (strcmp(audioid, line_hash) == 0) {
            // Found it! Parse the rest.
            ptr = next_tab + 1;
            
            // 2. Get Duration
            next_tab = strchr(ptr, '\t');
            if (!next_tab) continue;
            *next_tab = 0;
            int line_dur = atoi(ptr);
            ptr = next_tab + 1;
            
            // 3. Get Bitrate
            next_tab = strchr(ptr, '\t');
            if (!next_tab) continue;
            *next_tab = 0;
            int line_rate = atoi(ptr);
             ptr = next_tab + 1;
            
            // 4. Get Path
            char *line_path = ptr;
            // It might end with newline OR tab (if extra columns exist)
            char *path_end = line_path;
            while (*path_end != 0 && *path_end != '\t' && *path_end != '\n' && *path_end != '\r') {
                path_end++;
            }
            *path_end = 0;
            
            if (filepath != NULL) {
                strcpy(filepath, line_path);
            }
            if (duration != NULL) {
                *duration = line_dur;
            }
            if (avg_bitrate != NULL) {
                *avg_bitrate = line_rate;
            }
            ret = ESP_OK;
            break;
        }
    }

    free(line_buf);
    fclose(fd_db);

    return ret;
}

static void audiodb_sdcard_url_save_cb(void *user_data, char *url)
{
    if (url == NULL) {
        return;
    }
    if (strlen(url) <= strlen(SDCARD_FILE_PREV_NAME) + 1) {
        return;
    }

    const char *filepath = url + strlen(SDCARD_FILE_PREV_NAME);

    if (audiodb_file_exists(filepath)) {
        ESP_LOGI(TAG, "File already in audiodb: %s", filepath);
        return;
    }

    if (audiodb_file_save(filepath) == ESP_OK) {
        ESP_LOGI(TAG, "Added to audiodb: %s", filepath);
    }
}

// scan for new mp3 or flac files on the SD card
esp_err_t audiodb_scan(void)
{
    ESP_LOGI(TAG, "scan");

    FILE *f = fopen(FILE_AUDIODB, "r");
    if (f) {
        int count = 0;
        int ch;
        int last_ch = 0;
        while ((ch = fgetc(f)) != EOF) {
            if (ch == '\n') {
                count++;
            }
            last_ch = ch;
        }
        // Handle case where last line has no newline
        if (last_ch != '\n' && last_ch != 0) {
            count++;
        }

        ESP_LOGI(TAG, "Audio DB exists (%s) with %d entries. Skipping scan", FILE_AUDIODB, count);
        fclose(f);
        return ESP_OK;
    }

    // scan for mp3 files
    sdcard_scan(audiodb_sdcard_url_save_cb, "/sdcard", 0, (const char *[]){"mp3", "flac"},
                2, NULL);

    ESP_LOGI(TAG, "audiodb_scan done");

    return ESP_OK;
}

esp_err_t audiodb_stop(void)
{
    // nothing for now

    return ESP_OK;
}
