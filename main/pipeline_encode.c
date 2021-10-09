//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 02.10.2021.
//

#include <esp_err.h>
#include <esp_log.h>
#include <audio_pipeline.h>
#include <i2s_stream.h>
#include <minimodem_encoder.h>
#include <fatfs_stream.h>
#include <esp_peripherals.h>
#include <esp_event.h>
#include "pipeline_encode.h"
#include "pipeline.h"

#define PLAYBACK_RATE       48000
#define PLAYBACK_CHANNEL    2
#define PLAYBACK_BITS       16

static const char *TAG = "cf_pipeline_encode";

static audio_pipeline_handle_t pipeline = NULL;
//audio_element_handle_t i2s_stream_writer, mp3_decoder, fatfs_stream_reader, rsp_handle;
static audio_element_handle_t i2s_stream_writer, minimodem_encoder, fatfs_stream_reader;
static audio_element_state_t el_state = AEL_STATE_INIT;

esp_err_t pipeline_encode_start(audio_event_iface_handle_t evt, char *url)
{
    el_state = AEL_STATE_RUNNING;

    ESP_LOGI(TAG, "%s", __FUNCTION__);

    if (pipeline != NULL) {
        ESP_LOGE(TAG, "encode_stop: pipeline already started");
        return ESP_FAIL;
    }

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (pipeline == NULL) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[4.1] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.i2s_config.sample_rate = PLAYBACK_RATE; // codec chip consumes 60000 16-bit stereo samples per second in that mode

    //i2s_cfg.i2s_config.sample_rate = 41000; // codec chip consumes 51250 16-bit stereo samples per second in that mode
    // so it's x1.25
    // I don't know why

    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[4.2] Create minimodem encoder");
    minimodem_encoder_cfg_t minimodem_cfg = DEFAULT_MINIMODEM_ENCODER_CONFIG();
    minimodem_encoder = minimodem_encoder_init(&minimodem_cfg);

    /* ZL38063 audio chip on board of ESP32-LyraTD-MSC does not support 44.1 kHz sampling frequency,
       so resample filter has been added to convert audio data to other rates accepted by the chip.
       You can resample the data to 16 kHz or 48 kHz.
    */
    //ESP_LOGI(TAG, "[4.3] Create resample filter");
    //rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    //rsp_handle = rsp_filter_init(&rsp_cfg);

    ESP_LOGI(TAG, "[4.4] Create fatfs stream to read data from sdcard");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);
    audio_element_set_uri(fatfs_stream_reader, url);

    ESP_LOGI(TAG, "[4.5] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, minimodem_encoder, "minimodem");
    //audio_pipeline_register(pipeline, rsp_handle, "filter");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[4.6] Link it together [sdcard]-->fatfs_stream-->minimodem-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"file", "minimodem", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[5.1] Listen for all pipeline events");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[3.1] Set up  i2s clock");
    i2s_stream_set_clk(i2s_stream_writer, PLAYBACK_RATE, PLAYBACK_BITS, PLAYBACK_CHANNEL);

    ESP_LOGI(TAG, "[ * ] Starting audio pipeline");
    audio_pipeline_run(pipeline);

    return ESP_OK;
}

esp_err_t pipeline_encode_maybe_handle_event(audio_event_iface_handle_t evt, audio_event_iface_msg_t *msg)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    if (msg->source_type == AUDIO_ELEMENT_TYPE_ELEMENT) {
        // Set music info for a new song to be played
#if 0
        if (msg.source == (void *) mp3_decoder
                && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(mp3_decoder, &music_info);
                ESP_LOGI(TAG, "[ * ] Received music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                         music_info.sample_rates, music_info.bits, music_info.channels);
                audio_element_setinfo(i2s_stream_writer, &music_info);
                rsp_filter_set_src_info(rsp_handle, music_info.sample_rates, music_info.channels);
                continue;
            }
#endif
        // Advance to the next song when previous finishes
        if (msg->source == (void *)i2s_stream_writer
            && msg->cmd == AEL_MSG_CMD_REPORT_STATUS) {

            el_state = audio_element_get_state(i2s_stream_writer);

            if (el_state == AEL_STATE_FINISHED) {
                ESP_LOGI(TAG, "[ * ] Finished encode");

                audio_event_iface_msg_t msg_out = {0};
                msg_out.cmd = APP_MSG_ENCODE_FINISHED;
                msg_out.data = NULL;
                msg_out.data_len = 0;
                msg->source = NULL;
                msg->source_type = AUDIO_ELEMENT_TYPE_PLAYER;

                return audio_event_iface_sendout(evt, &msg_out);
            }
        }
    }

    return ESP_OK;
}

esp_err_t pipeline_encode_stop()
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);

    if (pipeline == NULL) {
        ESP_LOGE(TAG, "encode_stop: pipeline not started");
        return ESP_FAIL;
    }

    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);

    audio_pipeline_deinit(pipeline);
    pipeline = NULL;

    el_state = AEL_STATE_FINISHED;

    return ESP_OK;
}

void pipeline_encode_status(char *buf, int buf_size)
{
    ESP_LOGI(TAG, "%s", __FUNCTION__);
    // TODO use el_state
}

bool pipeline_encode_is_running(void)
{
    return el_state == AEL_STATE_RUNNING;
}
