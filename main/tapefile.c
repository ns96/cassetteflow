//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 07.10.2021.
//

#include <stdio.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <string.h>

#include "tapefile.h"
#include "internal.h"
#include "audiodb.h"

static const char *TAG = "cf_tapefile";

static const int replicate = 4;

static esp_err_t format_line(FILE *file,
                             const char *tape_id,
                             const char side,
                             int track_num,
                             const char *mp3_id,
                             int playtime,
                             int playtime_total)
{
    //1. 4 digit Tape ID + 1 letter side 'A' or 'B'
    //2. 2 digit track number
    //3. Auto generate 10 digit MP3 ID associated with a MP3 file, or http url [future addition]
    //4. 4 digit number indicating desired playtime (in seconds) of the MP3. A value of “000M”
    //indicates that the MP3 should be loaded, but not played to allow for delay between
    //each mp3 file.
    //5. 4 digit number indicating the total number of seconds played so far on tape.
    for (int i = 0; i < replicate; ++i) {
        int written = fprintf(file, "%4s%c_%02d_%10s_%04d_%04d\n",
                              tape_id, side, track_num, mp3_id, playtime, playtime_total);
        if (written <= 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static esp_err_t format_line_mute(FILE *file,
                                  const char *tape_id,
                                  const char side,
                                  int track_num,
                                  const char *mp3_id,
                                  const int mute_seconds,
                                  int playtime_total)
{
    //1. 4 digit Tape ID + 1 letter side 'A' or 'B'
    //2. 2 digit track number
    //3. Auto generate 10 digit MP3 ID associated with a MP3 file, or http url [future addition]
    //4. 4 digit number indicating desired playtime (in seconds) of the MP3. A value of “000M”
    //indicates that the MP3 should be loaded, but not played to allow for delay between
    //each mp3 file.
    //5. 4 digit number indicating the total number of seconds played so far on tape.
    int written = fprintf(file, "%4s%c_%02d_%10s_%03dM_%04d\n",
                          tape_id, side, track_num, mp3_id, mute_seconds, playtime_total);
    if (written <= 0) {
        return ESP_FAIL;
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
 * @param tape_length_minutes 60, 90, 110, or 120
 * @param data
 * @param mute_time mute time between tracks in seconds
 * @return ESP_OK, ESP_FAIL if file error, ESP_ERR_INVALID_SIZE - play time does not fit into tape,
 *  ESP_ERR_NOT_FOUND - mp3id not found in the DB
 */
esp_err_t tapefile_create(const char side, int tape_length_minutes, char *data, int mute_time)
{
    char *mp3id;
    char tape_id[TAPEFILE_LINE_LENGTH + 1];
    int mp3Count = 0;
    int timeTotal = 0;
    FILE *fd = NULL;
    // this file will be used to when adding to the tape DB
    FILE *fd_tapedb = NULL;
    const char *filepath = tapefile_get_path(side);
    esp_err_t ret = ESP_OK;

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
        fputc(side, fd_tapedb);
        fputc('\t', fd_tapedb);
    }

    while ((mp3id != NULL) && (ret == ESP_OK)) {
        int mp3_length_seconds = 0;
        if (audiodb_file_for_id(mp3id, NULL, &mp3_length_seconds, NULL) != ESP_OK) {
            // file not found in the DB
            ret = ESP_ERR_NOT_FOUND;
            continue;
        }

        // add line records to create a N second muted section before next song
        if (mp3Count >= 1) {
            if (format_line_mute(fd, tape_id, side, mp3Count + 1, mp3id, mute_time, timeTotal) != ESP_OK) {
                ret = ESP_FAIL;
                break;
            }
            timeTotal += mute_time;
        }

        for (int i = 0; i < mp3_length_seconds; ++i) {
            if (format_line(fd, tape_id, side, mp3Count + 1, mp3id, i, timeTotal) != ESP_OK) {
                ret = ESP_FAIL;
                break;
            }
            timeTotal += 1;
        }
        if (ret != ESP_OK) {
            continue;
        }

        // add to the tapeDB file
        fputs(mp3id, fd_tapedb);
        fputc('\t', fd_tapedb);

        mp3Count += 1;
        mp3id = strtok(NULL, ",");
    }

    if (ret == ESP_OK) {
        // check total play time to fit into the tape
        int tape_side_duration_seconds = tape_length_minutes * 60 / 2;
        if (timeTotal > tape_side_duration_seconds) {
            ret = ESP_ERR_INVALID_SIZE;
        }
    }

    fclose(fd);
    fclose(fd_tapedb);

    ESP_LOGI(TAG, "File creation complete");

    return ret;
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

    if (fscanf(fd, "%4s[AB]_", tapeid) != 1) {
        err = ESP_FAIL;
    }

    fclose(fd);
    return err;
}
