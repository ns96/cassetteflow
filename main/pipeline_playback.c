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
#include <flac_decoder.h>
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

static const char *TAG_DECODER_MP3 = "mp3";
static const char *TAG_DECODER_FLAC = "flac";
static const char *TAG_FILE_READER = "file_read";
static const char *TAG_EQUALIZER = "equalizer";
static const char *TAG_RESAMPLE = "resample";

#define USE_EQ  1

#define PLAYBACK_RATE       48000

//timer interval in us
#define READ_TIMER_INTERVAL (250000)

// time in millis to wait for new data from minimodem before considering the tape is stopped
#define MINIMODEM_WAIT_TIME (pdMS_TO_TICKS(1000))

// playback of mp3 files from sd card with equalizer
static audio_pipeline_handle_t pipeline_for_play = NULL;

static audio_element_handle_t *output_stream_writer = NULL;
static audio_element_handle_t fatfs_stream_reader = NULL,
    mp3_decoder = NULL, flac_decoder = NULL,
    equalizer = NULL, resample_for_play = NULL;

static audio_element_state_t el_state = AEL_STATE_STOPPED;
// -13 dB is minimum. 0 - no gain.
// The size of gain array should be the multiplication of NUMBER_BAND and number channels of audio stream data.
static int equalizer_band_gain[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static char current_playing_audio_id[11] = {0};
static char current_playing_audio_filepath[256];
static int current_playing_audio_duration = 0;
static int current_playing_audio_avg_bitrate = 0;

extern bool output_is_bt;

static esp_timer_handle_t periodic_timer = NULL;
static FILE *fd_side_file = NULL;
static int64_t pause_time_us = 0;

static enum pipeline_decoder_mode current_audio_type = PIPELINE_DECODER_MP3;

static audio_event_iface_handle_t evt_playback;
static bool stop_event_loop = false;

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
    audio_element_handle_t resample = rsp_filter_init(&rsp_cfg);
    return resample;
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
    current_audio_type = file_decoder;

    audio_pipeline_stop(pipeline_for_play);
    audio_pipeline_wait_for_stop(pipeline_for_play);

    audio_pipeline_reset_ringbuffer(pipeline_for_play);
    audio_pipeline_reset_elements(pipeline_for_play);
    ESP_LOGI(TAG, "Breakup pipeline");
    audio_pipeline_breakup_elements(pipeline_for_play, NULL);

    int link_num;
    char **link_tag = make_link_tag(&link_num);
    ESP_LOGI(TAG, "[5] Link new pipeline");
    audio_pipeline_relink(pipeline_for_play, (const char **)link_tag, link_num);
    free(link_tag);

    ESP_LOGI(TAG, "[-] Listening event from pipelines");
    ESP_ERROR_CHECK(audio_pipeline_set_listener(pipeline_for_play, evt_playback));

    ESP_ERROR_CHECK(esp_event_post_to(pipeline_event_loop, PIPELINE_EVENTS,
                                      PIPELINE_PLAYBACK_STARTED, NULL, 0, portMAX_DELAY));
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

    ESP_LOGI(TAG, "[1] Create fatfs_stream_reader");
    char *output_stream_name = NULL;
    if (pipeline_output_init_stream(&output_stream_writer, &output_stream_name) == ESP_FAIL) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[2] Create fatfs_stream_reader");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_cfg.task_core = 1;
    fatfs_cfg.task_prio = 10;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    if (fatfs_stream_reader == NULL) {
        ESP_LOGE(TAG, "error init fatfs_stream_reader");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[3] Create mp3_decoder");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_core = 1;
    mp3_cfg.task_prio = 10;
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    if (mp3_decoder == NULL) {
        ESP_LOGE(TAG, "error init mp3_decoder");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[3.1] Create flac decoder");
    flac_decoder_cfg_t flac_dec_cfg = DEFAULT_FLAC_DECODER_CONFIG();
    flac_dec_cfg.task_core = 1;
    flac_dec_cfg.task_prio = 10;
    flac_decoder = flac_decoder_init(&flac_dec_cfg);
    if (flac_decoder == NULL) {
        ESP_LOGE(TAG, "error init flac_decoder");
        return ESP_FAIL;
    }

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
    audio_pipeline_register(pipeline_for_play, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline_for_play, flac_decoder, "flac");
#ifdef USE_EQ
    audio_pipeline_register(pipeline_for_play, equalizer, "equalizer");
#endif
    audio_pipeline_register(pipeline_for_play, resample_for_play, "resample");
    audio_pipeline_register(pipeline_for_play, *output_stream_writer, output_stream_name);

    ESP_LOGI(TAG, "[7] Link it together [sdcard]-->fatfs_stream-->decoder-->equalizer-->resample--> %s stream", output_stream_name);
    int link_num;
    char **link_tag = make_link_tag(&link_num);
    audio_pipeline_link(pipeline_for_play, (const char **)link_tag, link_num);
    free(link_tag);
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
    char audio_id[11];
    int playtime_seconds;   // seconds
    int playtime_total_seconds; // seconds

    // b. Once a line of data is processed, then start playback of the indicated MP3 file at the indicated time.
    //  As more lines of data are read from the cassette, compare the MP3 ID/time to the one currently playing.
    //  If they match (need to see how accurately this needs to be in sync, but I think within +/- 2 seconds should be fine) continue playing.

    if (sscanf(line, "%4s%c_%02d_%10s_%04d_%04d",
               tape_id, &side, &track_num, audio_id, &playtime_seconds, &playtime_total_seconds) != 6) {
        //check for pause line
        int pause_sec = 0;
        if (sscanf(line, "%4s%c_%02d_%10s_%03dM_%04d",
                   tape_id, &side, &track_num, audio_id, &pause_sec, &playtime_total_seconds) != 6) {
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
    int current_playing_audio_time_seconds = 0;
    audio_element_state_t state = audio_element_get_state(*output_stream_writer);
    audio_element_info_t fatfs_music_info = {0};

    if (state == AEL_STATE_RUNNING) {
        // pipeline is playing, get current mp3 time
        audio_element_getinfo(fatfs_stream_reader, &fatfs_music_info);
        if (current_playing_audio_avg_bitrate > 0) {
            current_playing_audio_time_seconds = fatfs_music_info.byte_pos / (current_playing_audio_avg_bitrate / 8);
        }
    }

    if (strcmp(audio_id, current_playing_audio_id) == 0) {
        if (abs(playtime_seconds - current_playing_audio_time_seconds) <= 2) {
            ESP_LOGI(TAG, "already playing this file");
            return ESP_OK;
        }
    } else {
        // read mp3 info from the audio DB
        if (audiodb_file_for_id(audio_id, current_playing_audio_filepath,
                                &current_playing_audio_duration,
                                &current_playing_audio_avg_bitrate) != ESP_OK) {
            ESP_LOGE(TAG, "could get file for audioid: %s", audio_id);
            return ESP_FAIL;
        }
    }

    if (playtime_seconds > 0) {
        ESP_LOGI(TAG, "seek to: %d, current time: %d", playtime_seconds, current_playing_audio_time_seconds);
        fatfs_byte_pos = (int)((int64_t)playtime_seconds * (int64_t)current_playing_audio_avg_bitrate / 8);
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

    strcpy(current_playing_audio_id, audio_id);

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

    evt_playback = evt;
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
    ESP_ERROR_CHECK(audio_pipeline_set_listener(pipeline_for_play, evt_playback));

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

        // Stop when receives stop event
        if (stop_event_loop) {
            stop_event_loop = false;
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
        }

        //process BT messages
        if (msg.source_type == PERIPH_ID_BLUETOOTH) {
            bt_process_events(msg);
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

    stop_event_loop = true;

    if (pipeline_for_play) {
        pipeline_output_deinit(pipeline_for_play, &output_stream_writer);
        pipeline_for_play = NULL;
    }

    if (fd_side_file != NULL) {
        fclose(fd_side_file);
        fd_side_file = NULL;
    }
    //reset current audiofile id
    current_playing_audio_id[0] = 0;

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
    stop_event_loop = true;

    if (pipeline_for_play != NULL) {
        audio_pipeline_stop(pipeline_for_play);
        audio_pipeline_wait_for_stop(pipeline_for_play);
    }

    int link_num;
    char **link_tag = make_link_tag(&link_num);

    if (!enable) {
        err = pipeline_output_set_bt(false, pipeline_for_play,
                                     &output_stream_writer, &resample_for_play,
                                     link_tag, link_num);
        pipeline_playback_unpause();
    } else {
        bt_set_device(device, device_len);
        err = pipeline_output_set_bt(true, pipeline_for_play,
                                     &output_stream_writer, &resample_for_play,
                                     link_tag, link_num);
    }
    free(link_tag);
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
