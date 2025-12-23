//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_err.h>
#include <audio_event_iface.h>
#include <esp_log.h>
#include <audio_pipeline.h>
#include <i2s_stream.h>
#include <fatfs_stream.h>
#include <equalizer.h>
#include <mp3_decoder.h>
#include <flac_decoder.h>
#include <filter_resample.h>
#include <string.h>
#include "pipeline_decode.h"
#include "pipeline.h"
#include "minimodem_config.h"
#include "minimodem_decoder.h"
#include "filter_line_reader.h"
#include "audiodb.h"
#include "raw_queue.h"
#include "pipeline_output.h"
#include "bt.h"
#include "tapefile.h"

static const char *TAG = "cf_pipeline_decode";

static const char *TAG_DECODER_MP3 = "mp3";
static const char *TAG_DECODER_FLAC = "flac";
static const char *TAG_FILE_READER = "file_read";
static const char *TAG_EQUALIZER = "equalizer";
static const char *TAG_RESAMPLE = "resample";

#define USE_EQ  1

#define PLAYBACK_RATE       48000

// time in millis to wait for new data from minimodem before considering the tape is stopped
#define MINIMODEM_WAIT_TIME (pdMS_TO_TICKS(500))

// playback of mp3 files from sd card with equalizer
static audio_pipeline_handle_t pipeline_for_play = NULL;
// record audio from line-in, decode with minimodem and output line by line (raw output)
static audio_pipeline_handle_t pipeline_for_record = NULL;
static audio_element_handle_t output_stream_writer = NULL;
static audio_element_handle_t fatfs_stream_reader = NULL,
    mp3_decoder = NULL, flac_decoder = NULL,
    equalizer = NULL, resample_for_play = NULL;
static audio_element_handle_t i2s_stream_reader = NULL, resample_for_record = NULL, minimodem_decoder = NULL,
    filter_line_reader = NULL;
static audio_element_state_t el_state = AEL_STATE_STOPPED;
// -13 dB is minimum. 0 - no gain.
// The size of gain array should be the multiplication of NUMBER_BAND and number channels of audio stream data.
int equalizer_band_gain[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static char current_playing_audio_id[11] = {0};
static char current_playing_audio_filepath[256];
static int current_playing_audio_duration = 0;
static int current_playing_audio_avg_bitrate = 0;

static char last_line_from_minimodem[64] = {0};
// in microseconds
static int64_t last_line_from_minimodem_time_us = 0;

static FILE *g_mapped_file = NULL;
static char g_mapped_side = 0;
// We track the last read line's timestamp to optimize sequential access
static int g_mapped_last_total_idx = -1;

static bool pause_decode = false;
static bool dct_mapping_enabled = false;

static enum pipeline_decoder_mode current_audio_type = PIPELINE_DECODER_MP3;


static audio_event_iface_handle_t evt_playback;
static esp_err_t pipeline_decode_handle_no_line_data(void);

static char **make_link_tag(int *tags_number)
{
#ifdef USE_EQ
    int tags_count = 5;
    char **tag = malloc(sizeof(char*) * tags_count);
    tag[0] = (char *)TAG_FILE_READER;
    if (current_audio_type == PIPELINE_DECODER_MP3) {
        tag[1] = (char *)TAG_DECODER_MP3;
    } else {
        tag[1] = (char *)TAG_DECODER_FLAC;
    }
    tag[2] = (char *)TAG_EQUALIZER;
    tag[3] = (char *)TAG_RESAMPLE;
    tag[4] = pipeline_output_get_stream_name();

#else
    int tags_count = 4;
    char **tag = malloc(sizeof(char*) * tags_count);
    tag[0] = (char *)TAG_FILE_READER;
    if (current_audio_type == PIPELINE_DECODER_MP3) {
        tag[1] = (char *)TAG_DECODER_MP3;
    } else {
        tag[1] = (char *)TAG_DECODER_FLAC;
    }
    tag[2] = (char *)TAG_RESAMPLE;
    tag[3] = pipeline_output_get_stream_name();
#endif
    *tags_number = tags_count;
    return tag;
}

static audio_element_handle_t resampler_init(void)
{
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = PLAYBACK_RATE;
    rsp_cfg.src_ch = 2;
    if (pipeline_output_is_bt()) {
        rsp_cfg.dest_rate = 44100;
    } else {
        rsp_cfg.dest_rate = PLAYBACK_RATE;
    }
    rsp_cfg.dest_ch = 2;
    rsp_cfg.mode = RESAMPLE_DECODE_MODE;
    rsp_cfg.complexity = 0;
    rsp_cfg.task_core = 1;
    rsp_cfg.task_prio = 10;
    audio_element_handle_t resample = rsp_filter_init(&rsp_cfg);
    return resample;
}

static esp_err_t flac_init(audio_pipeline_handle_t pipeline)
{
    ESP_LOGI(TAG, "[3.1] Create flac decoder");
    flac_decoder_cfg_t flac_dec_cfg = DEFAULT_FLAC_DECODER_CONFIG();
    flac_dec_cfg.task_core = 1;
    flac_dec_cfg.task_prio = 10;
    flac_decoder = flac_decoder_init(&flac_dec_cfg);
    if (flac_decoder == NULL) {
        ESP_LOGE(TAG, "error init flac_decoder");
        return ESP_FAIL;
    }

    audio_pipeline_register(pipeline_for_play, flac_decoder, "flac");
    current_audio_type = PIPELINE_DECODER_FLAC;
    return ESP_OK;
}

static esp_err_t mp3_init(audio_pipeline_handle_t pipeline)
{
    ESP_LOGI(TAG, "[3] Create mp3_decoder");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_core = 1;
    mp3_cfg.task_prio = 10;
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    if (mp3_decoder == NULL) {
        ESP_LOGE(TAG, "error init mp3_decoder");
        return ESP_FAIL;
    }
    audio_pipeline_register(pipeline_for_play, mp3_decoder, "mp3");
    current_audio_type = PIPELINE_DECODER_MP3;
    return ESP_OK;
}

static void set_playback_decoder(audio_pipeline_handle_t pipeline, const char *filepath)
{
    enum pipeline_decoder_mode file_decoder;

    ESP_LOGI(TAG, "%s", __FUNCTION__);

    if (pipeline_for_play == NULL) {
        return;
    }

    if (strcmp((filepath + strlen(filepath) - 3), "mp3") == 0) {
        file_decoder = PIPELINE_DECODER_MP3;
    } else if (strcmp((filepath + strlen(filepath) - 4), "flac") == 0) {
        file_decoder = PIPELINE_DECODER_FLAC;
    } else {
        ESP_LOGE(TAG, "Unknown audio file extension %s", filepath);
        return;
    }

    //current decoder already set to needed decoder
    if (current_audio_type == file_decoder) {
        return;
    }

    ESP_LOGI(TAG, "Relink pipeline");
    audio_pipeline_breakup_elements(pipeline, NULL);

    //deinit old decoder and init new one
    if (current_audio_type == PIPELINE_DECODER_MP3) {
        if (mp3_decoder) {
            audio_pipeline_unregister(pipeline, mp3_decoder);
            audio_element_deinit(mp3_decoder);
            mp3_decoder = NULL;
        }
        flac_init(pipeline);
    } else {
        if (flac_decoder) {
            audio_pipeline_unregister(pipeline, flac_decoder);
            audio_element_deinit(flac_decoder);
            flac_decoder = NULL;
        }
        mp3_init(pipeline);
    }

    int link_num;
    char **link_tag = make_link_tag(&link_num);
    audio_pipeline_relink(pipeline, (const char **)link_tag, link_num);
    free(link_tag);

    ESP_LOGI(TAG, "[-] Listening event from pipelines");
    ESP_ERROR_CHECK(audio_pipeline_set_listener(pipeline, evt_playback));
}

static esp_err_t create_playback_pipeline(void)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    if (pipeline_for_play != NULL) {
        ESP_LOGE(TAG, "pipeline_for_play: pipeline already started");
        return ESP_FAIL;
    }

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_for_play = audio_pipeline_init(&pipeline_cfg);
    if (pipeline_for_play == NULL) {
        ESP_LOGE(TAG, "error init pipeline_for_play");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[1] Create output stream writer");
    char *output_stream_name = NULL;
    if (pipeline_output_init_stream(&output_stream_writer, &output_stream_name) == ESP_FAIL) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[2] Create fatfs_stream_reader");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_cfg.task_core = 1;
    fatfs_cfg.task_prio = 10;
    fatfs_cfg.ext_stack = true;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    if (fatfs_stream_reader == NULL) {
        ESP_LOGE(TAG, "error init fatfs_stream_reader");
        return ESP_FAIL;
    }

    //use default mp3 decoder
    mp3_init(pipeline_for_play);

#ifdef USE_EQ
    ESP_LOGI(TAG, "[4] Create equalizer");
    equalizer_cfg_t eq_cfg = DEFAULT_EQUALIZER_CONFIG();
    eq_cfg.channel = 2;
    eq_cfg.set_gain = equalizer_band_gain;
    eq_cfg.task_core = 1;
    eq_cfg.task_prio = 10;
    equalizer = equalizer_init(&eq_cfg);
    if (equalizer == NULL) {
        ESP_LOGE(TAG, "error init equalizer");
        return ESP_FAIL;
    }
#endif

    ESP_LOGI(TAG, "[5] Create resampler");
    resample_for_play = resampler_init();
    if (resample_for_play == NULL) {
        ESP_LOGE(TAG, "error init resampler");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[6] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline_for_play, fatfs_stream_reader, "file_read");

#ifdef USE_EQ
    audio_pipeline_register(pipeline_for_play, equalizer, "equalizer");
#endif
    audio_pipeline_register(pipeline_for_play, resample_for_play, "resample");
    audio_pipeline_register(pipeline_for_play, output_stream_writer, output_stream_name);

    ESP_LOGI(TAG, "[7] Link it together [sdcard]-->fatfs_stream-->decoder-->equalizer-->resample--> %s stream", output_stream_name);
    int link_num;
    char **link_tag = make_link_tag(&link_num);
    audio_pipeline_link(pipeline_for_play, (const char **)link_tag, link_num);
    free(link_tag);

    return ESP_OK;
}

static esp_err_t create_record_pipeline(void)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__ );

    if (pipeline_for_record != NULL) {
        ESP_LOGE(TAG, "pipeline_for_record: pipeline already started");
        return ESP_FAIL;
    }

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_for_record = audio_pipeline_init(&pipeline_cfg);
    if (pipeline_for_record == NULL) {
        ESP_LOGE(TAG, "error init pipeline_for_record");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[1] Create i2s_stream_reader");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_cfg.i2s_config.sample_rate = PLAYBACK_RATE;
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);
    if (i2s_stream_reader == NULL) {
        ESP_LOGE(TAG, "error init i2s_stream_reader");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[2] Create resampler");
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = PLAYBACK_RATE;
    rsp_cfg.src_ch = 2;
    rsp_cfg.dest_rate = 16000;
    rsp_cfg.dest_ch = 2;
    rsp_cfg.mode = RESAMPLE_DECODE_MODE;
    rsp_cfg.complexity = 0;
    rsp_cfg.task_core = 1;
    rsp_cfg.task_prio = 10;
    resample_for_record = rsp_filter_init(&rsp_cfg);
    if (resample_for_record == NULL) {
        ESP_LOGE(TAG, "error init resampler");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[3] Create minimodem_decoder");
    minimodem_decoder_cfg_t minimodem_decoder_cfg = DEFAULT_MINIMODEM_DECODER_CONFIG();
    minimodem_decoder_cfg.task_prio = 10;
    minimodem_decoder_cfg.stack_in_ext = false; // keep minimodem's stack in internal memory
    minimodem_decoder = minimodem_decoder_init(&minimodem_decoder_cfg);
    if (minimodem_decoder == NULL) {
        ESP_LOGE(TAG, "error init minimodem_decoder");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[4] Create filter_line_reader");
    filter_line_cfg_t line_reader_cfg = DEFAULT_FILTER_LINE_CONFIG();
    line_reader_cfg.task_prio = 10;
    line_reader_cfg.stack_in_ext = true;
    filter_line_reader = filter_line_reader_init(&line_reader_cfg);
    if (filter_line_reader == NULL) {
        ESP_LOGE(TAG, "error init filter_line_reader");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[5] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline_for_record, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline_for_record, resample_for_record, "resample");
    audio_pipeline_register(pipeline_for_record, minimodem_decoder, "minimodem");
#if 0
    audio_pipeline_register(pipeline_for_record, raw_reader, "raw_read");
#endif
    audio_pipeline_register(pipeline_for_record, filter_line_reader, "line_reader");

    ESP_LOGI(TAG, "[6] Link it together i2s_stream-->resample->minimodem-->line_reader");
    const char *link_tag[4] = {"i2s", "resample", "minimodem", "line_reader"};
    audio_pipeline_link(pipeline_for_record, link_tag, 4);

    return ESP_OK;
}




static esp_err_t find_mapped_line(const char side, int total_idx, char *out_line) {
    // 1. Open/Reset file if needed
    if (g_mapped_file != NULL && g_mapped_side != side) {
        fclose(g_mapped_file);
        g_mapped_file = NULL;
        g_mapped_last_total_idx = -1;
    }

    if (g_mapped_file == NULL) {
        const char *filepath = tapefile_get_path(side);
        g_mapped_file = fopen(filepath, "r");
        if (!g_mapped_file) {
            ESP_LOGE(TAG, "Failed to open side file: %s", filepath);
            return ESP_FAIL;
        }
        g_mapped_side = side;
        g_mapped_last_total_idx = -1;
    }

    // 2. Check if we need to rewind
    // If we are looking for a timestamp BEFORE where we last were, we must rewind.
    // (Unless it's the same index, in which case we might just need to re-read? 
    //  But typically we read line-by-line. If we just read line X at T, and we want T again, 
    //  we might be at T+1 in the file. So safely, we should rewind if total_idx <= g_mapped_last_total_idx.
    //  However, rewinding every time for the same second (which has 4 DCT lines) is inefficient.
    //  BUT, `g_mapped_last_total_idx` tracks the line we *just read*.
    //  So if we read T=100. File ptr is at lines for T=101.
    //  Next request T=100. We are ahead. We MUST rewind.
    //  Optimization: If we have a mechanism to peek or buffer, we could avoid this. 
    //  Given the file is small, let's try just rewind. 
    //  Improving this: Since 1s has 4 lines, maybe we shouldn't advance blindly?
    //  Actually, we scan UNTIL we find a match.
    //  If we found a match for T=100, we leave the file ptr at the NEXT line.
    //  If next request is T=100, we check the next line. 
    //  If next line is ALSO T=100 (which it is, for audio), we match!
    //  So we only rewind if total_idx < g_mapped_last_total_idx.
    
    if (total_idx < g_mapped_last_total_idx) {
        ESP_LOGD(TAG, "Rewinding... target %d < last %d", total_idx, g_mapped_last_total_idx);
        fseek(g_mapped_file, 0, SEEK_SET);
        g_mapped_last_total_idx = -1;
    }

    char line[128];
    long pos_before_line;
    
    // 3. Scan forward
    while (1) {
        pos_before_line = ftell(g_mapped_file);
        if (!fgets(line, sizeof(line), g_mapped_file)) {
            // End of file
            break;
        }
        
        // clean line
        line[strcspn(line, "\r\n")] = 0;
        
        char tape_id[5];
        char line_side;
        int track_num;
        char mp3_id[11];
        int playtime_seconds;
        int playtime_total_seconds;
        int mute_seconds;
        bool match = false;
        
        // Check Standard Audio Line
        if (sscanf(line, "%4s%c_%02d_%10s_%04d_%04d",
                   tape_id, &line_side, &track_num, mp3_id, &playtime_seconds, &playtime_total_seconds) == 6) {
             
             if (playtime_total_seconds == total_idx) {
                 match = true;
             } else if (playtime_total_seconds > total_idx) {
                 // We passed it. Target not found (or we missed it).
                 // But wait, since we only move forward, this implies it's not here.
                 // However, we must be careful about the "replicated 4 times" logic.
                 // If total_idx=100. We find 101. 
                 // Then 100 is effectively missing from the current position onwards.
                 // Since we handle rewind at the top, if we are here, we started from <= target.
                 // So if we see > target, we failed.
                 // BUT: We should push back the line we just read so we don't skip it for next time?
                 // fseek(g_mapped_file, pos_before_line, SEEK_SET);
                 // No, if we return FAIL, caller might handle it.
                 // But to keep state consistent, probably best to rewind one line?
                 // Let's just break and return FAIL.
                 // But update last_idx?
                 g_mapped_last_total_idx = playtime_total_seconds; // we are at 101
                 // We need to rewind this line so next call for 101 captures it!
                 fseek(g_mapped_file, pos_before_line, SEEK_SET);
                 return ESP_FAIL; 
             }
             
             // Update our tracking
             g_mapped_last_total_idx = playtime_total_seconds;
        } 
        // Check Mute Line
        else if (sscanf(line, "%4s%c_%02d_%10s_%03dM_%04d",
                   tape_id, &line_side, &track_num, mp3_id, &mute_seconds, &playtime_total_seconds) == 6) {
             
             // Mute line acts as a range [playtime_total_seconds, playtime_total_seconds + mute_seconds)
             if (total_idx >= playtime_total_seconds && total_idx < (playtime_total_seconds + mute_seconds)) {
                 match = true;
             } else if (playtime_total_seconds > total_idx) {
                 // We passed it.
                 g_mapped_last_total_idx = playtime_total_seconds;
                 fseek(g_mapped_file, pos_before_line, SEEK_SET);
                 return ESP_FAIL;
             }
             
             // Update tracking. 
             // If we matched, we are conceptually "at" total_idx.
             // If we are inside the mute range, say [100, 104), and target is 102.
             // Next call 103. We need to match this SAME line again.
             // So we must REWIND this line if we want to match it again!
             // Mute lines appear only ONCE.
             // So if we consumed it for 100, we lose it for 101, 102, 103!
             // CRITICAL FIX: If matched MUTE line, we must rewind so next call sees it too?
             // Or we just don't advance?
             // If we fseek back, we just re-read it endlessly. That's fine if we increment target.
             
             if (match) {
                 // For mute, the SINGLE line covers multiple seconds. 
                 // So we must NOT consume it until we are past it.
                 // Reset file ptr to start of this line.
                 fseek(g_mapped_file, pos_before_line, SEEK_SET);
                 g_mapped_last_total_idx = total_idx; // Effectively we are here
             } else {
                 // If we didn't match (meaning target < start of mute, which shouldn't happen if we scan sequentially? 
                 // or target >= start+duration -> we passed it).
                 g_mapped_last_total_idx = playtime_total_seconds + mute_seconds - 1; 
             }
        }

        if (match) {
            strcpy(out_line, line);
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

/**
 * Handle line of decoded text from minimodem
 * @param line
 * @return
 */
/**
 * Handle line of decoded text from minimodem
 * @param line
 * @param prefix prefix for raw output (e.g. "--> " for mapped lines)
 * @return
 */
static esp_err_t pipeline_decode_handle_line_internal(const char *line, const char *prefix)
{
    const size_t line_len = strlen(line);

    raw_queue_message_t msg;
    if (prefix && prefix[0] != 0) {
        snprintf(msg.line, sizeof(msg.line), "%s%s", prefix, line);
    } else {
        strcpy(msg.line, line);
    }
    raw_queue_send(0, &msg);

    strcpy(last_line_from_minimodem, line);
    last_line_from_minimodem_time_us = esp_timer_get_time();

    if (line_len != TAPEFILE_LINE_LENGTH) {
        ESP_LOGE(TAG, "unexpected line_len: %d", (int)line_len);
        return ESP_FAIL;
    }

    char tape_id[5];
    char side;
    int track_num;
    char mp3_id[11];
    int playtime_seconds;   // seconds
    int playtime_total_seconds; // seconds
    int mute_seconds = 0;

    // Check for Mute line first
    if (sscanf(line, "%4s%c_%02d_%10s_%03dM_%04d",
                   tape_id, &side, &track_num, mp3_id, &mute_seconds, &playtime_total_seconds) == 6) {
        ESP_LOGI(TAG, "Mute line detected: %s (duration %d)", line, mute_seconds);
        // Stop playback if playing
        pipeline_decode_handle_no_line_data();
        return ESP_OK;
    }

    // b. Once a line of data is processed, then start playback of the indicated MP3 file at the indicated time.
    //  As more lines of data are read from the cassette, compare the MP3 ID/time to the one currently playing.
    //  If they match (need to see how accurately this needs to be in sync, but I think within +/- 2 seconds should be fine) continue playing.

    if (sscanf(line, "%4s%c_%02d_%10s_%04d_%04d",
               tape_id, &side, &track_num, mp3_id, &playtime_seconds, &playtime_total_seconds) != 6) {
        ESP_LOGE(TAG, "could not decode line");
        return ESP_FAIL;
    }

    //do not precess lines in pause state
    if (pause_decode) {
        return ESP_OK;
    }

    int fatfs_byte_pos = 0; // start from the beginning by default
    int current_playing_audio_time_seconds = 0;
    audio_element_state_t state = audio_element_get_state(output_stream_writer);
    audio_element_info_t fatfs_music_info = {0};
    if (state == AEL_STATE_RUNNING) {
        // pipeline is playing, get current mp3 time
        audio_element_getinfo(fatfs_stream_reader, &fatfs_music_info);
        if (current_playing_audio_avg_bitrate > 0) {
            current_playing_audio_time_seconds = fatfs_music_info.byte_pos / (current_playing_audio_avg_bitrate / 8);
        }
    }

    if (strcmp(mp3_id, current_playing_audio_id) == 0) {
        if (abs(playtime_seconds - current_playing_audio_time_seconds) <= 10) {
            ESP_LOGI(TAG, "already playing this file");
            return ESP_OK;
        }
    } else {
        // check that the mp3_id is not the special dynamic content track ID
        if (strcmp(mp3_id, "aaaaaaaaaa") == 0) {
            ESP_LOGI(TAG, "Processing Dynamic Content Track ...");
            
            if (dct_mapping_enabled) {
                char mapped_line[128];
                if (find_mapped_line(side, playtime_total_seconds, mapped_line) == ESP_OK) {
                    ESP_LOGI(TAG, "DCT Mapped to: %s", mapped_line);
                    // Recursively handle the mapped line with prefix
                    return pipeline_decode_handle_line_internal(mapped_line, "--> "); 
                } else {
                    ESP_LOGW(TAG, "DCT Mapping not found for totaltime %d", playtime_total_seconds);
                    // Do NOT stop playback; just ignore this DCT line and keep playing whatever is playing.
                }
            }
            return ESP_OK;
        }

        // if we couldn't get mp3/flac file from the mp3 DB, log error
        if (audiodb_file_for_id(mp3_id, current_playing_audio_filepath,
                                &current_playing_audio_duration,
                                &current_playing_audio_avg_bitrate) != ESP_OK) {
            ESP_LOGE(TAG, "could not get file for audioId: %s", mp3_id);
            return ESP_FAIL;
        }
    }

    if (playtime_seconds > 0) {
        ESP_LOGI(TAG, "seek to: %d, current time: %d", playtime_seconds, current_playing_audio_time_seconds);
        fatfs_byte_pos = (int64_t)playtime_seconds * (int64_t)current_playing_audio_avg_bitrate / 8;
    }

    // c. If the line data MP3 ID/time does not match, then switch to the indicated MP3 file/time and start playing.
    switch (state) {
        case AEL_STATE_INIT:
            audio_element_set_uri(fatfs_stream_reader, current_playing_audio_filepath);
            audio_element_set_byte_pos(fatfs_stream_reader, fatfs_byte_pos);
            set_playback_decoder(pipeline_for_play, current_playing_audio_filepath);
            audio_pipeline_run(pipeline_for_play);
            break;
        case AEL_STATE_RUNNING:
            audio_pipeline_stop(pipeline_for_play);
            audio_pipeline_wait_for_stop(pipeline_for_play);
            /* fallthrough */
        case AEL_STATE_FINISHED:
        case AEL_STATE_ERROR:
            audio_pipeline_reset_ringbuffer(pipeline_for_play);
            audio_pipeline_reset_elements(pipeline_for_play);
            audio_pipeline_change_state(pipeline_for_play, AEL_STATE_INIT);
            audio_element_set_uri(fatfs_stream_reader, current_playing_audio_filepath);
            audio_element_set_byte_pos(fatfs_stream_reader, fatfs_byte_pos);
            set_playback_decoder(pipeline_for_play, current_playing_audio_filepath);
            audio_pipeline_run(pipeline_for_play);
            break;
        default:
            ESP_LOGE(TAG, "unhandled state %d", state);
            break;
    }

    strcpy(current_playing_audio_id, mp3_id);

    return ESP_OK;
}

static esp_err_t pipeline_decode_handle_line(const char *line)
{
    return pipeline_decode_handle_line_internal(line, "");
}

static esp_err_t pipeline_decode_handle_no_line_data(void)
{
    // d. If no line data is being received i.e. the cassette tape was stopped, then stop playback of the current MP3 and wait for more data.

    audio_element_state_t state = audio_element_get_state(output_stream_writer);
    switch (state) {
        case AEL_STATE_NONE:
        case AEL_STATE_INIT:
            /* nothing to do here */
            break;
        case AEL_STATE_INITIALIZING:
        case AEL_STATE_RUNNING:
        case AEL_STATE_PAUSED:
            audio_pipeline_stop(pipeline_for_play);
            audio_pipeline_wait_for_stop(pipeline_for_play);
            /* fallthrough */
        case AEL_STATE_STOPPED:
        case AEL_STATE_FINISHED:
        case AEL_STATE_ERROR:
            audio_pipeline_reset_ringbuffer(pipeline_for_play);
            audio_pipeline_reset_elements(pipeline_for_play);
            audio_pipeline_change_state(pipeline_for_play, AEL_STATE_INIT);
            break;
        default:
            ESP_LOGE(TAG, "pipeline_decode_handle_no_line_data: unhandled state %d", state);
            break;
    }

    // reset current playing info
    current_playing_audio_id[0] = 0;
    last_line_from_minimodem_time_us = 0;

    return ESP_OK;
}


esp_err_t pipeline_decode_start(audio_event_iface_handle_t evt)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__ );

    el_state = AEL_STATE_RUNNING;
    esp_err_t err;

    evt_playback = evt;
    pipeline_decode_unpause();

    err = create_playback_pipeline();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "error create_playback_pipeline");
        return ESP_FAIL;
    }

    err = create_record_pipeline();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "error create_record_pipeline");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[-] Listening event from pipelines");
    ESP_ERROR_CHECK(audio_pipeline_set_listener(pipeline_for_play, evt));
    ESP_ERROR_CHECK(audio_pipeline_set_listener(pipeline_for_record, evt));

    ESP_LOGI(TAG, "[-] Start audio_pipeline");
//    ESP_ERROR_CHECK(audio_pipeline_run(pipeline_for_play));
    ESP_ERROR_CHECK(audio_pipeline_run(pipeline_for_record));

    ESP_ERROR_CHECK(esp_event_post_to(pipeline_event_loop, PIPELINE_EVENTS,
                                      PIPELINE_DECODE_STARTED, NULL, 0, portMAX_DELAY));

    return ESP_OK;
}

esp_err_t pipeline_decode_event_loop(audio_event_iface_handle_t evt)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    while (1) {
        audio_event_iface_msg_t msg = {0};

        esp_err_t ret = audio_event_iface_listen(evt, &msg, MINIMODEM_WAIT_TIME);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Timeout waiting for line data");

            // need to add message so that http server can send line data indicating
            // that playback was stopped.
            raw_queue_message_t msg;
            strcpy(msg.line, "### NOCARRIER");
            raw_queue_send(0, &msg);

            if (last_line_from_minimodem_time_us > 0
                    && esp_timer_get_time() - last_line_from_minimodem_time_us > MINIMODEM_WAIT_TIME * 1000) {
                pipeline_decode_handle_no_line_data();
            }
            continue;
        }
        ESP_LOGD(TAG, "%s event:%d", __FUNCTION__, msg.cmd);

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
                && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            if (msg.source == (void *)mp3_decoder) {
                audio_element_getinfo(mp3_decoder, &music_info);
            } else if (msg.source == (void *)flac_decoder) {
                audio_element_getinfo(flac_decoder, &music_info);
            } else {
                continue;
            }

            ESP_LOGI(TAG, "[ * ] Receive music info from decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

#ifdef USE_EQ
            equalizer_set_info(equalizer, music_info.sample_rates, music_info.channels);
#endif
            rsp_filter_set_src_info(resample_for_play, music_info.sample_rates, music_info.channels);
//            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            memset(&msg, 0, sizeof(audio_event_iface_msg_t));
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)filter_line_reader
            && msg.cmd == AEL_MSG_CMD_REPORT_POSITION) {
            // we got a text line from minimodem decoder
            char *line = audio_element_get_uri(filter_line_reader);
            ESP_LOGI(TAG, "[ * ] line=%s", line);
            pipeline_decode_handle_line(line);
            continue;
        }

        // Stop when the last pipeline element (i2s_stream_reader in this case) receives stop event
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && (msg.source == (void *)i2s_stream_reader)
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && ((int)msg.data == AEL_STATUS_STATE_FINISHED)) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
        }

        // process BT messages
        bt_process_events(msg);
    }

    return ESP_OK;
}

esp_err_t pipeline_decode_stop(void)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    if (pipeline_for_record != NULL) {
        // stop event loop
        audio_element_report_status(i2s_stream_reader, AEL_STATUS_STATE_FINISHED);

        audio_pipeline_stop(pipeline_for_record);
        audio_pipeline_wait_for_stop(pipeline_for_record);

        audio_pipeline_deinit(pipeline_for_record);
        pipeline_for_record = NULL;
    }

    if (pipeline_for_play != NULL) {
        pipeline_output_deinit(pipeline_for_play, &output_stream_writer);
        pipeline_for_play = NULL;
    }

    // reset current playing info
    current_playing_audio_id[0] = 0;

    el_state = AEL_STATE_STOPPED;

    // close mapped file if open
    if (g_mapped_file) {
        fclose(g_mapped_file);
        g_mapped_file = NULL;
    }

    return ESP_OK;
}

void pipeline_decode_status(char *buf, size_t buf_size)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    // state of playback
    audio_element_state_t state = audio_element_get_state(output_stream_writer);

    if (state == AEL_STATE_RUNNING) {
        // returns “DECODE” and the current line record if playing
        snprintf(buf, buf_size, "DECODE %s", last_line_from_minimodem);
    } else {
        // If nothing is playing, returns “playback stopped”.
        snprintf(buf, buf_size, "playback stopped");
    }
}

/**
 * 10 bands, channels are equal
 * @return ESP_OK or error
 */
esp_err_t pipeline_decode_set_equalizer(int band_gain[10])
{
    esp_err_t ret = ESP_OK;

    for (int i = 0; i < 10; ++i) {
        if (pipeline_for_play != NULL && equalizer != NULL) {
            ret = equalizer_set_gain_info(equalizer, i, band_gain[i], true);
            if (ret != ESP_OK) {
                break;
            }
        }
    }

    return ret;
}

void pipeline_decode_pause(void)
{
    ESP_LOGI(TAG, "Pause Decode");
    pause_decode = true;
}

void pipeline_decode_unpause(void)
{
    ESP_LOGI(TAG, "Resume Decode");
    pause_decode = false;
}

void pipeline_decode_set_dct_mapping(bool enabled)
{
    dct_mapping_enabled = enabled;
    ESP_LOGI(TAG, "DCT Mapping: %s", enabled ? "ENABLED" : "DISABLED");
    if (!enabled) {
        // Close mapped file to reset state
        if (g_mapped_file) {
            fclose(g_mapped_file);
            g_mapped_file = NULL;
            g_mapped_last_total_idx = -1;
        }
    }
}