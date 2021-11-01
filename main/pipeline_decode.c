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
#include <raw_stream.h>
#include <filter_resample.h>
#include <string.h>
#include "pipeline_decode.h"
#include "pipeline.h"
#include "minimodem_config.h"
#include "minimodem_decoder.h"

static const char *TAG = "cf_pipeline_decode";

#define PLAYBACK_RATE       48000

// playback of mp3 files from sd card with equalizer
static audio_pipeline_handle_t pipeline_for_play = NULL;
// record audio from line-in, decode with minimodem and output line by line (raw output)
static audio_pipeline_handle_t pipeline_for_record = NULL;
static audio_element_handle_t i2s_stream_writer = NULL, fatfs_stream_reader = NULL, mp3_decoder = NULL,
    equalizer = NULL, resample_for_play = NULL;
static audio_element_handle_t i2s_stream_reader = NULL, minimodem_decoder = NULL, raw_reader = NULL;
static audio_element_state_t el_state = AEL_STATE_STOPPED;
// -13 dB is minimum. 0 - no gain.
// The size of gain array should be the multiplication of NUMBER_BAND and number channels of audio stream data.
static int equalizer_band_gain[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

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

    ESP_LOGI(TAG, "[1] Create i2s_stream_writer");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.i2s_config.sample_rate = PLAYBACK_RATE;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    if (i2s_stream_writer == NULL) {
        ESP_LOGE(TAG, "error init i2s_stream_writer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[2] Create fatfs_stream_reader");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);
    // FIXME
    audio_element_set_uri(fatfs_stream_reader, "/sdcard/02. The Grid (Feat. Jeff Bridges).mp3");

    if (fatfs_stream_reader == NULL) {
        ESP_LOGE(TAG, "error init fatfs_stream_reader");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[3] Create mp3_decoder");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_core = 1;
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    if (mp3_decoder == NULL) {
        ESP_LOGE(TAG, "error init mp3_decoder");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[4] Create equalizer");
    equalizer_cfg_t eq_cfg = DEFAULT_EQUALIZER_CONFIG();
    eq_cfg.channel = 2;
    eq_cfg.set_gain = equalizer_band_gain;
    eq_cfg.task_core = 1;
    equalizer = equalizer_init(&eq_cfg);
    if (equalizer == NULL) {
        ESP_LOGE(TAG, "error init equalizer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[5] Create resampler");
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = PLAYBACK_RATE;
    rsp_cfg.src_ch = 2;
    rsp_cfg.dest_rate = PLAYBACK_RATE;
    rsp_cfg.dest_ch = 2;
    rsp_cfg.mode = RESAMPLE_DECODE_MODE;
    rsp_cfg.complexity = 0;
    rsp_cfg.task_core = 1;
    resample_for_play = rsp_filter_init(&rsp_cfg);
    if (resample_for_play == NULL) {
        ESP_LOGE(TAG, "error init resampler");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[6] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline_for_play, fatfs_stream_reader, "file_read");
    audio_pipeline_register(pipeline_for_play, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline_for_play, equalizer, "equalizer");
    audio_pipeline_register(pipeline_for_play, resample_for_play, "resample");
    audio_pipeline_register(pipeline_for_play, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[7] Link it together [sdcard]-->fatfs_stream-->mp3_decoder-->equalizer-->resample-->i2s_stream");
    const char *link_tag[5] = {"file_read", "mp3", "equalizer", "resample", "i2s"};
    audio_pipeline_link(pipeline_for_play, link_tag, 5);

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

    ESP_LOGI(TAG, "[2] Create minimodem_decoder");
    minimodem_decoder_cfg_t minimodem_decoder_cfg = DEFAULT_MINIMODEM_DECODER_CONFIG();
    minimodem_decoder = minimodem_decoder_init(&minimodem_decoder_cfg);
    if (minimodem_decoder == NULL) {
        ESP_LOGE(TAG, "error init minimodem_decoder");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[3] Create raw_reader");
    raw_stream_cfg_t raw_asr_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_asr_cfg.type = AUDIO_STREAM_READER;
    raw_reader = raw_stream_init(&raw_asr_cfg);
    if (raw_reader == NULL) {
        ESP_LOGE(TAG, "error init raw_reader");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline_for_record, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline_for_record, minimodem_decoder, "minimodem");
    audio_pipeline_register(pipeline_for_record, raw_reader, "raw_read");

    ESP_LOGI(TAG, "[5] Link it together i2s_stream-->minimodem-->raw_reader");
    const char *link_tag[3] = {"i2s", "minimodem", "raw_read"};
    audio_pipeline_link(pipeline_for_record, link_tag, 3);

    return ESP_OK;
}

esp_err_t pipeline_decode_start(audio_event_iface_handle_t evt)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__ );

    el_state = AEL_STATE_RUNNING;
    esp_err_t err;

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
    ESP_ERROR_CHECK(audio_pipeline_run(pipeline_for_play));
//    audio_pipeline_pause(pipeline_for_play);

    ESP_ERROR_CHECK(audio_pipeline_run(pipeline_for_record));

    ESP_ERROR_CHECK(esp_event_post_to(pipeline_event_loop, PIPELINE_EVENTS,
                                      PIPELINE_DECODE_STARTED, NULL, 0, portMAX_DELAY));

    return ESP_OK;
}

esp_err_t pipeline_decode_event_loop(audio_event_iface_handle_t evt)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    char buff[TAPEFILE_LINE_LENGTH + 1] = {0};
    while (1) {
        audio_event_iface_msg_t msg;
        // TODO a. If no data is being received, then do nothing.
        audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        // TODO b. Once a line of data is processed, then start playback of the indicated MP3 file at the indicated time.
        //  As more lines of data are read from the cassette, compare the MP3 ID/time to the one currently playing.
        //  If they match (need to see how accurately this needs to be in sync, but I think within +/- 2 seconds should be fine) continue playing.
//        raw_stream_read(raw_reader, buff, TAPEFILE_LINE_LENGTH);

        // TODO c. If the line data MP3 ID/time does not match, then switch to the indicated MP3 file/time and start playing.
        // TODO d. If no line data is being received i.e. the cassette tape was stopped, then stop playback of the current MP3 and wait for more data.

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            equalizer_set_info(equalizer, music_info.sample_rates, music_info.channels);
            rsp_filter_set_src_info(resample_for_play, music_info.sample_rates, music_info.channels);
//            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            memset(&msg, 0, sizeof(audio_event_iface_msg_t));
            continue;
        }

        // Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && (msg.source == (void *)i2s_stream_writer
            || msg.source == (void *)i2s_stream_reader)
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
        }
    }

    return ESP_OK;
}

esp_err_t pipeline_decode_stop(void)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    if (pipeline_for_play != NULL) {
        audio_pipeline_stop(pipeline_for_play);
        audio_pipeline_wait_for_stop(pipeline_for_play);

        audio_pipeline_deinit(pipeline_for_play);
        pipeline_for_play = NULL;
    }

    if (pipeline_for_record != NULL) {
        audio_pipeline_stop(pipeline_for_record);
        audio_pipeline_wait_for_stop(pipeline_for_record);

        audio_pipeline_deinit(pipeline_for_record);
        pipeline_for_record = NULL;
    }

    el_state = AEL_STATE_STOPPED;

    return ESP_OK;
}

void pipeline_decode_status(char *buf, size_t buf_size)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    //TODO
}

/**
 * 10 bands, channels are equal
 * @return ESP_OK or error
 */
esp_err_t pipeline_decode_set_equalizer(int band_gain[10])
{
    esp_err_t ret = ESP_OK;

    for (int i = 0; i < 10; ++i) {
        equalizer_band_gain[i] = band_gain[i];
        equalizer_band_gain[i + 10] = band_gain[i];

        if (pipeline_for_play != NULL) {
            ret = equalizer_set_gain_info(equalizer, i, band_gain[i], true);
            if (ret != ESP_OK) {
                break;
            }
        }
    }

    return ret;
}
