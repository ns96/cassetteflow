//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#include <stdio.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <string.h>

#include "tapefile.h"
#include "internal.h"

static const char *TAG = "cf_tapefile";

static const int replicate = 4;

static esp_err_t format_line(FILE *file, const char *tape_id, int track_num,
                             const char *mp3_id, int playtime, int playtime_total)
{
    //1. 5 digit Tape ID
    //2. 2 digit track number
    //3. Auto generate 10 digit MP3 ID associated with a MP3 file, or http url [future addition]
    //4. 4 digit number indicating desired playtime (in seconds) of the MP3. A value of “000M”
    //indicates that the MP3 should be loaded, but not played to allow for delay between
    //each mp3 file.
    //5. 4 digit number indicating the total number of seconds played so far on tape.
    for (int i = 0; i < replicate; ++i) {
        int written = fprintf(file, "%5s_%02d_%10s_%04d_%04d\n", tape_id, track_num, mp3_id, playtime, playtime_total);
        if (written <= 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static esp_err_t format_line_mute(FILE *file, const char *tape_id, int track_num,
                                  const char *mp3_id, int playtime_total)
{
    //1. 5 digit Tape ID
    //2. 2 digit track number
    //3. Auto generate 10 digit MP3 ID associated with a MP3 file, or http url [future addition]
    //4. 4 digit number indicating desired playtime (in seconds) of the MP3. A value of “000M”
    //indicates that the MP3 should be loaded, but not played to allow for delay between
    //each mp3 file.
    //5. 4 digit number indicating the total number of seconds played so far on tape.
    for (int i = 0; i < replicate; ++i) {
        int written = fprintf(file, "%5s_%02d_%10s_000M_%04d\n", tape_id, track_num, mp3_id, playtime_total);
        if (written <= 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

const char *tapefile_get_path(const char side)
{
    switch (side) {
        case 'a':
        case 'A':
        default:
            return FILENAME_SIDE_A;
        case 'b':
        case 'B':
            return FILENAME_SIDE_B;
    }
}

const char *tapefile_get_path_tapedb(const char side)
{
    switch (side) {
        case 'a':
        case 'A':
        default:
            return FILENAME_SIDE_A_TAPEDB;
        case 'b':
        case 'B':
            return FILENAME_SIDE_B_TAPEDB;
    }
}

/**
 * generates the text file for side A or B to be encoded onto a 60, 90, 110, or 120
 * minute tape
 * @param side a or b (lowercase)
 * @param tape 60, 90, 110, or 120
 * @param data
 * @return
 */
esp_err_t tapefile_create(const char side, const char *tape, char *data, int mute_time)
{
    char *mp3id;
    char tape_id[32];
    int mp3Count = 0;
    int timeTotal = 0;
    FILE *fd = NULL;
    // this file will be used to when adding to the tape DB
    FILE *fd_tapedb = NULL;
    const char *filepath = tapefile_get_path(side);

    ESP_LOGI(TAG, "Creating text file for side:%c", side);

    fd = fopen(filepath, "w");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to create file : %s", filepath);
        return ESP_FAIL;
    }

    fd_tapedb = fopen(tapefile_get_path_tapedb(side), "w");
    if (!fd_tapedb) {
        ESP_LOGE(TAG, "Failed to create file : %s", filepath);
        fclose(fd);
        return ESP_FAIL;
    }

    // split data (first is tapeId)
    mp3id = strtok(data, ",");
    if (mp3id != NULL) {
        // copy tapeId
        strlcpy(tape_id, mp3id, sizeof(tape_id));
        // split next data (comma separated MP3 IDs)
        mp3id = strtok(NULL, ",");
    }

    if (mp3id != NULL) {
        fputs(tape_id, fd_tapedb);
        fputc('\t', fd_tapedb);
    }

    while (mp3id != NULL) {
        // add line records to create a N second muted section before next song
        if (mp3Count >= 1) {
            for (int i = 0; i < mute_time; ++i) {
                timeTotal += 1;
                if (format_line_mute(fd, tape_id, mp3Count + 1, mp3id, timeTotal) != ESP_OK) {
                    // TODO error
                }
            }
        }

        // TODO get length from mp3db for the mp3id
        int length_seconds = 10;

        for (int i = 0; i < length_seconds; ++i) {
            if (format_line(fd, tape_id, mp3Count + 1, mp3id, i, timeTotal) != ESP_OK) {
                // TODO error
            }
            timeTotal += 1;
        }

        // add to the tapeDB file
        fputs(mp3id, fd_tapedb);
        fputc('\t', fd_tapedb);

        mp3Count += 1;
        mp3id = strtok(NULL, ",");
    }

    fputc('\n', fd_tapedb);

    fclose(fd);
    fclose(fd_tapedb);

    ESP_LOGI(TAG, "File creation complete");

    return ESP_OK;
}

bool tapefile_is_present(const char side)
{
    struct stat file_stat;
    const char *filepath = tapefile_get_path(side);

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        return false;
    }

    return true;
}

/**
 * Read tapeid from current tape file (if present)
 * @param side a or b
 * @param tapeid at least 6 bytes
 * @return
 */
esp_err_t tapefile_read_tapeid(const char side, char *tapeid)
{
    FILE *fd = NULL;
    const char *filepath = tapefile_get_path(side);
    esp_err_t err = ESP_OK;

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        return ESP_FAIL;
    }

    if (fscanf(fd, "%5s_", tapeid) != 1) {
        err = ESP_FAIL;
    }

    fclose(fd);
    return err;
}
