//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <stdio.h>
#include <stdbool.h>
#include <esp_log.h>
#include <string.h>
#include "tapedb.h"
#include "tapefile.h"
#include "internal.h"

static const char *TAG = "cf_tapedb";

/**
 * Save file to the DB
 * @param side (of sideA_tapedb.txt or sideB_tapedb.txt)
 * @return
 */
esp_err_t tapedb_file_save(const char side)
{
    FILE *fd;
    FILE *fd_db;
    char buf[256];
    size_t bytes_read;
    const char *filename = tapefile_get_path_tapedb(side);

    fd = fopen(filename, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open file : %s", filename);
        return ESP_FAIL;
    }

    fd_db = fopen(FILE_TAPEDB, "a");
    if (!fd_db) {
        ESP_LOGE(TAG, "Failed to open file : %s", filename);
        fclose(fd);
        return ESP_FAIL;
    }

    while ((bytes_read = fread(buf, 1, sizeof(buf), fd)) > 0) {
        fwrite(buf, 1, bytes_read, fd_db);
    }
    fputc('\n', fd_db);

    fclose(fd_db);
    fclose(fd);

    return ESP_OK;
}

/**
 * Check if file present in the DB
 * @param tape tape ID
 * @return true if tape is already in the db
 */
bool tapedb_tape_exists(const char *tape)
{
    FILE *fd_db;
    char line_tape_id[32];
    bool found = false;

    fd_db = fopen(FILE_TAPEDB, "r");
    if (!fd_db) {
        ESP_LOGE(TAG, "Failed to open DB : %s", FILE_TAPEDB);
        return false;
    }

    // read DB line by line (only tape ids)
    while (fscanf(fd_db, "%5s\t%*[^\n]\n", line_tape_id) == 1) {
        if (strcmp(tape, line_tape_id) == 0) {
            // file present in the DB
            found = true;
            break;
        }
        // skip until next line
        int ch;
        do {
            ch = fgetc(fd_db);
        }
        while (ch != EOF && ch != '\n');
    }

    fclose(fd_db);

    return found;
}
