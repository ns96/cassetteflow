//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#include "pipeline_playback.h"
#include <esp_err.h>
#include <audio_event_iface.h>
#include <esp_log.h>
#include <audio_pipeline.h>
#include <i2s_stream.h>
#include <fatfs_stream.h>
#include <equalizer.h>
#include <mp3_decoder.h>
#include <raw_stream.h>
#include <filter_resample.h>
#include <string.h>
#include "pipeline_decode.h"
#include "pipeline.h"
#include "minimodem_config.h"
#include "audiodb.h"
#include "raw_queue.h"
#include "pipeline_output.h"
#include "bt.h"

static const char *TAG = "cf_pipeline_playback";

#define USE_EQ  1

#define PLAYBACK_RATE       48000

//timer interval in us
#define READ_TIMER_INTERVAL (250000)

// time in millis to wait for new data from minimodem before considering the tape is stopped
#define MINIMODEM_WAIT_TIME (pdMS_TO_TICKS(1000))

// playback of mp3 files from sd card with equalizer
static audio_pipeline_handle_t pipeline_for_play = NULL;

static audio_element_handle_t *output_stream_writer = NULL;
static audio_element_handle_t fatfs_stream_reader = NULL, mp3_decoder = NULL,
    equalizer = NULL, resample_for_play = NULL;

static audio_element_state_t el_state = AEL_STATE_STOPPED;
// -13 dB is minimum. 0 - no gain.
// The size of gain array should be the multiplication of NUMBER_BAND and number channels of audio stream data.
static int equalizer_band_gain[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static char current_playing_mp3_id[11] = {0};
static char current_playing_mp3_filepath[256];
static int current_playing_mp3_duration = 0;
static int current_playing_mp3_avg_bitrate = 0;

extern bool output_is_bt;

static esp_timer_handle_t periodic_timer = NULL;
static FILE *fd_side_file = NULL;
static int64_t pause_time_us = 0;


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

    ESP_LOGI(TAG, "[1] Create fatfs_stream_reader");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_cfg.task_core = 1;
    fatfs_cfg.task_prio = 10;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    if (fatfs_stream_reader == NULL) {
        ESP_LOGE(TAG, "error init fatfs_stream_reader");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[2] Create mp3_decoder");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_core = 1;
    mp3_cfg.task_prio = 10;
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    if (mp3_decoder == NULL) {
        ESP_LOGE(TAG, "error init mp3_decoder");
        return ESP_FAIL;
    }

#ifdef USE_EQ
    ESP_LOGI(TAG, "[3] Create equalizer");
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

    ESP_LOGI(TAG, "[4] Create resampler");
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = PLAYBACK_RATE;
    rsp_cfg.src_ch = 2;
    if (output_is_bt) {
        rsp_cfg.dest_rate = 44100;
    } else {
        rsp_cfg.dest_rate = PLAYBACK_RATE;
    }
    rsp_cfg.dest_ch = 2;
    rsp_cfg.mode = RESAMPLE_DECODE_MODE;
    rsp_cfg.complexity = 0;
    rsp_cfg.task_core = 1;
    rsp_cfg.task_prio = 10;
    resample_for_play = rsp_filter_init(&rsp_cfg);
    if (resample_for_play == NULL) {
        ESP_LOGE(TAG, "error init resampler");
        return ESP_FAIL;
    }

    char output_stream_name[10] = {0};
    if (pipeline_output_init_stream(&output_stream_writer, output_stream_name) == ESP_FAIL) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[6] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline_for_play, fatfs_stream_reader, "file_read");
    audio_pipeline_register(pipeline_for_play, mp3_decoder, "mp3");
#ifdef USE_EQ
    audio_pipeline_register(pipeline_for_play, equalizer, "equalizer");
#endif
    audio_pipeline_register(pipeline_for_play, resample_for_play, "resample");
    audio_pipeline_register(pipeline_for_play, *output_stream_writer, output_stream_name);

    ESP_LOGI(TAG, "[7] Link it together [sdcard]-->fatfs_stream-->mp3_decoder-->equalizer-->resample--> %s stream", output_stream_name);
#ifdef USE_EQ
    const char *link_tag[5] = {"file_read", "mp3", "equalizer", "resample", output_stream_name};
    audio_pipeline_link(pipeline_for_play, link_tag, 5);
#else
    const char *link_tag[4] = {"file_read", "mp3", "resample", output_stream};
    audio_pipeline_link(pipeline_for_play, link_tag, 4);
#endif

    return ESP_OK;
}

/**
 * Handle line of text from file
 * @param line
 * @return
 */
static esp_err_t pipeline_playback_handle_line(const char *line)
{
    const size_t line_len = strlen(line);

    raw_queue_message_t msg;
    strcpy(msg.line, line);
    raw_queue_send(0, &msg);

    if (line_len != TAPEFILE_LINE_LENGTH) {
        ESP_LOGE(TAG, "unexpected line_len: %zu", line_len);
        return ESP_FAIL;
    }

    char tape_id[5];
    char side;
    int track_num;
    char mp3_id[11];
    int playtime_seconds;   // seconds
    int playtime_total_seconds; // seconds

    // b. Once a line of data is processed, then start playback of the indicated MP3 file at the indicated time.
    //  As more lines of data are read from the cassette, compare the MP3 ID/time to the one currently playing.
    //  If they match (need to see how accurately this needs to be in sync, but I think within +/- 2 seconds should be fine) continue playing.

    if (sscanf(line, "%4s%c_%02d_%10s_%04d_%04d",
               tape_id, &side, &track_num, mp3_id, &playtime_seconds, &playtime_total_seconds) != 6) {
        //check for pause line
        int pause_sec = 0;
        if (sscanf(line, "%4s%c_%02d_%10s_%03dM_%04d",
                   tape_id, &side, &track_num, mp3_id, &pause_sec, &playtime_total_seconds) != 6) {
            ESP_LOGE(TAG, "could not decode line");
            return ESP_FAIL;
        }
        if (pause_sec > 0) {
            ESP_LOGI(TAG, "Set pause time: %d sec", pause_sec);
            //pause playback
            audio_pipeline_stop(pipeline_for_play);
            audio_pipeline_wait_for_stop(pipeline_for_play);
            pause_time_us = (int64_t)pause_sec * 1000000;
            return ESP_OK;
        }
    }

    int fatfs_byte_pos = 0; // start from the beginning by default
    int current_playing_mp3_time_seconds = 0;
    audio_element_state_t state = audio_element_get_state(*output_stream_writer);
    audio_element_info_t fatfs_music_info = {0};

    if (state == AEL_STATE_RUNNING) {
        // pipeline is playing, get current mp3 time
        audio_element_getinfo(fatfs_stream_reader, &fatfs_music_info);
        if (current_playing_mp3_avg_bitrate > 0) {
            current_playing_mp3_time_seconds = fatfs_music_info.byte_pos / (current_playing_mp3_avg_bitrate / 8);
        }
    }

    if (strcmp(mp3_id, current_playing_mp3_id) == 0) {
        if (abs(playtime_seconds - current_playing_mp3_time_seconds) <= 2) {
            ESP_LOGI(TAG, "already playing this file");
            return ESP_OK;
        }
    } else {
        // read mp3 info from the mp3 DB
        if (mp3db_file_for_id(mp3_id, current_playing_mp3_filepath,
                              &current_playing_mp3_duration,
                              &current_playing_mp3_avg_bitrate) != ESP_OK) {
            ESP_LOGE(TAG, "could get file for mp3id: %s", mp3_id);
            return ESP_FAIL;
        }
    }

    if (playtime_seconds > 0) {
        ESP_LOGI(TAG, "seek to: %d, current time: %d", playtime_seconds, current_playing_mp3_time_seconds);
        fatfs_byte_pos = (int64_t)playtime_seconds * (int64_t)current_playing_mp3_avg_bitrate / 8;
    }

    // c. If the line data MP3 ID/time does not match, then switch to the indicated MP3 file/time and start playing.
    switch (state) {
        case AEL_STATE_INIT:
            audio_element_set_uri(fatfs_stream_reader, current_playing_mp3_filepath);
            audio_element_set_byte_pos(fatfs_stream_reader, fatfs_byte_pos);
            audio_pipeline_run(pipeline_for_play);
            break;
        case AEL_STATE_RUNNING:
            audio_pipeline_stop(pipeline_for_play);
            audio_pipeline_wait_for_stop(pipeline_for_play);
            /* fallthrough */
        case AEL_STATE_FINISHED:
            audio_pipeline_reset_ringbuffer(pipeline_for_play);
            audio_pipeline_reset_elements(pipeline_for_play);
            audio_pipeline_change_state(pipeline_for_play, AEL_STATE_INIT);
            audio_element_set_uri(fatfs_stream_reader, current_playing_mp3_filepath);
            audio_element_set_byte_pos(fatfs_stream_reader, fatfs_byte_pos);
            audio_pipeline_run(pipeline_for_play);
            break;
        default:
            ESP_LOGE(TAG, "unhandled state %d", state);
            break;
    }

    strcpy(current_playing_mp3_id, mp3_id);

    return ESP_OK;
}

static void periodic_timer_callback(void* arg)
{
    char line[128] = {0};

    //do not read next lines in pause state
    if (pause_time_us > 0) {
        ESP_LOGI(TAG, "[ * ] pause elapsed time %d s", (int)(pause_time_us/1000000));
        pause_time_us -= READ_TIMER_INTERVAL;
        if (pause_time_us > 0) {
            return;
        }
        pause_time_us = 0;
        audio_pipeline_run(pipeline_for_play);
    }

    if (!fgets(line, 126, fd_side_file)) {
        //can`t read any new line
//        pipeline_playback_handle_no_line_data();
        pipeline_playback_stop();
        fclose(fd_side_file);
        fd_side_file = NULL;
    }
    // remove trailing newline character(s)
    line[strcspn(line, "\r\n")] = 0;

    ESP_LOGI(TAG, "[ * ] line=%s", line);

    pipeline_playback_handle_line(line);
}

esp_err_t pipeline_playback_start(audio_event_iface_handle_t evt, const char *filename)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__ );

    el_state = AEL_STATE_RUNNING;
    esp_err_t err;

    pause_time_us = 0;

    fd_side_file = fopen(filename, "r");
    if (!fd_side_file) {
        ESP_LOGE(TAG, "Failed to open DB : %s", FILE_TAPEDB);
        return false;
    }

    err = create_playback_pipeline();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "error create_playback_pipeline");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[-] Listening event from pipelines");
    ESP_ERROR_CHECK(audio_pipeline_set_listener(pipeline_for_play, evt));

    ESP_LOGI(TAG, "[-] Start audio_pipeline");
    ESP_ERROR_CHECK(audio_pipeline_run(pipeline_for_play));

    ESP_ERROR_CHECK(esp_event_post_to(pipeline_event_loop, PIPELINE_EVENTS,
                                      PIPELINE_PLAYBACK_STARTED, NULL, 0, portMAX_DELAY));

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &periodic_timer_callback,
        /* name is optional, but may help identify the timer when debugging */
        .name = "periodic"
    };

    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    /* The timer has been created but is not running yet */
    /* Start the timer */
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, READ_TIMER_INTERVAL));

    return ESP_OK;
}

esp_err_t pipeline_playback_event_loop(audio_event_iface_handle_t evt)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    while (1) {
        audio_event_iface_msg_t msg = {0};

        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }
        ESP_LOGD(TAG, "%s event:%d", __FUNCTION__, msg.cmd);

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

#ifdef USE_EQ
            equalizer_set_info(equalizer, music_info.sample_rates, music_info.channels);
#endif
            rsp_filter_set_src_info(resample_for_play, music_info.sample_rates, music_info.channels);
//            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            memset(&msg, 0, sizeof(audio_event_iface_msg_t));
            continue;
        }

        //process BT messages
        if (msg.source_type == PERIPH_ID_BLUETOOTH) {
            if (bt_process_events(msg) == 1) {
                break;
            }
        }
    }

    return ESP_OK;
}

esp_err_t pipeline_playback_stop(void)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    /* Clean up and finish timer */
    if (periodic_timer != NULL) {
        ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
        ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
        periodic_timer = NULL;
    }

    if (pipeline_for_play) {
        pipeline_output_deinit(pipeline_for_play, &output_stream_writer);
        ESP_LOGI(TAG, "Set NULL");
        pipeline_for_play = NULL;
    }

    if (fd_side_file != NULL) {
        fclose(fd_side_file);
        fd_side_file = NULL;
    }

    el_state = AEL_STATE_STOPPED;

    return ESP_OK;
}

esp_err_t pipeline_playback_set_output_bt(bool enable, const char *device, size_t device_len)
{
    esp_err_t err = ESP_FAIL;

    if (!enable && !output_is_bt) {
        //output already SP. Do nothing
        return ESP_OK;
    }
    pipeline_playback_pause();

    if (pipeline_for_play != NULL) {
        audio_pipeline_stop(pipeline_for_play);
        audio_pipeline_wait_for_stop(pipeline_for_play);
    }

    if (!enable) {
#ifdef USE_EQ
        const char *link_tag[5] = {"file_read", "mp3", "equalizer", "resample", "i2s"};
        int link_num = 5;
#else
        const char *link_tag[4] = {"file_read", "mp3", "resample", "i2s"};
        int link_num = 4;
#endif

        err = pipeline_output_set_bt(false, pipeline_for_play, &output_stream_writer, link_tag, link_num);
        pipeline_playback_unpause();
        return err;
    } else {
#ifdef USE_EQ
        const char *link_tag[5] = {"file_read", "mp3", "equalizer", "resample", "bt"};
        int link_num = 5;
#else
        const char *link_tag[4] = {"file_read", "mp3", "resample", "bt"};
        int link_num = 4;
#endif
        bt_set_device(device, device_len);
        err = pipeline_output_set_bt(true, pipeline_for_play, &output_stream_writer, link_tag, link_num);
    }
    ESP_ERROR_CHECK(esp_event_post_to(pipeline_event_loop, PIPELINE_EVENTS,
                                      PIPELINE_PLAYBACK_STARTED, NULL, 0, portMAX_DELAY));


    return err;
}

void pipeline_playback_pause(void)
{
    //pause playback
    ESP_LOGI(TAG, "Pause Playback");
    ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
}

void pipeline_playback_unpause(void)
{
    ESP_LOGI(TAG, "Resume Playback");
    esp_timer_start_periodic(periodic_timer, READ_TIMER_INTERVAL);
}
