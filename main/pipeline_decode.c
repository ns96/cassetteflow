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

static const char *TAG = "cf_pipeline_decode";

// playback of mp3 files from sd card with equalizer
static audio_pipeline_handle_t pipeline_for_play = NULL;
// record audio from line-in, decode with minimodem and output line by line (raw output)
static audio_pipeline_handle_t pipeline_for_record = NULL;
static audio_element_handle_t i2s_stream_writer, fatfs_stream_reader, mp3_decoder, equalizer, resample_for_play;
static audio_element_handle_t i2s_stream_reader, minimodem_decoder, raw_reader;
static audio_element_state_t el_state = AEL_STATE_STOPPED;

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
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[1] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.i2s_config.sample_rate = 48000; // codec chip consumes 60000 16-bit stereo samples per second in that mode
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[2] Create fatfs stream to read data from sdcard");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);
//    audio_element_set_uri(fatfs_stream_reader, url);

    mp3_decoder_cfg_t mp3_sdcard_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_sdcard_cfg);

    ESP_LOGI(TAG, "[3] Create equalizer");
    equalizer_cfg_t eq_cfg = DEFAULT_EQUALIZER_CONFIG();
    int set_gain[] =
        {-13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13, -13};
    eq_cfg.set_gain =
        set_gain; // The size of gain array should be the multiplication of NUMBER_BAND and number channels of audio stream data. The minimum of gain is -13 dB.
    equalizer = equalizer_init(&eq_cfg);

    ESP_LOGI(TAG, "[5] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline_for_play, fatfs_stream_reader, "file_read");
    audio_pipeline_register(pipeline_for_play, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline_for_play, equalizer, "equalizer");
//    audio_pipeline_register(pipeline_for_play, resample_for_play, "filter");
    audio_pipeline_register(pipeline_for_play, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[3.2] Link it together [sdcard]-->fatfs_stream-->mp3_decoder-->equalizer-->i2s_stream");
    audio_pipeline_link(pipeline_for_play, (const char *[]){
        "file_read", "mp3", "equalizer", "i2s"
    }, 4);

    return ESP_OK;
}

static esp_err_t create_record_pipeline(void)
{
    if (pipeline_for_record != NULL) {
        ESP_LOGE(TAG, "pipeline_for_record: pipeline already started");
        return ESP_FAIL;
    }

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_for_record = audio_pipeline_init(&pipeline_cfg);
    if (pipeline_for_record == NULL) {
        return ESP_FAIL;
    }

    i2s_stream_cfg_t i2s_file_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_file_cfg.type = AUDIO_STREAM_READER;
    i2s_file_cfg.i2s_config.sample_rate = 48000;
    i2s_stream_reader = i2s_stream_init(&i2s_file_cfg);

    // TODO minimodem

    raw_stream_cfg_t raw_asr_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_asr_cfg.type = AUDIO_STREAM_READER;
    raw_reader = raw_stream_init(&raw_asr_cfg);

    audio_pipeline_register(pipeline_for_record, i2s_stream_reader, "i2s");
//    audio_pipeline_register(pipeline_for_record, resample_for_rec, "filter");
    audio_pipeline_register(pipeline_for_record, raw_reader, "raw_read");

    const char *link_tag[3] = {"i2s", "filter", "raw_read"};
    audio_pipeline_link(pipeline_for_record, &link_tag[0], 3);

    return ESP_OK;
}

esp_err_t pipeline_decode_start(audio_event_iface_handle_t evt)
{
    el_state = AEL_STATE_RUNNING;

    create_playback_pipeline();
    create_record_pipeline();

    ESP_LOGI(TAG, "[4.1] Listening event from pipelines");
    audio_pipeline_set_listener(pipeline_for_play, evt);
    audio_pipeline_set_listener(pipeline_for_record, evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline_for_play);
    audio_pipeline_run(pipeline_for_record);

    esp_event_post_to(pipeline_event_loop, PIPELINE_EVENTS, PIPELINE_DECODE_STARTED, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t pipeline_decode_event_loop(audio_event_iface_handle_t evt)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    char buff[960] = {0};
    while (1) {
        audio_event_iface_msg_t msg;
        audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        // FIXME
        raw_stream_read(raw_reader, buff, 960);

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            audio_element_setinfo(i2s_stream_writer, &music_info);
//            rsp_filter_set_src_info(resample_for_play, music_info.sample_rates, music_info.channels);
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

void pipeline_decode_status(char *buf, int buf_size)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    //TODO
}
