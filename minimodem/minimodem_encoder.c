// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
// All rights reserved.

#include "esp_log.h"
#include "audio_mem.h"
#include "audio_element.h"
#include "minimodem_encoder.h"
#include "audio_error.h"

static const char *TAG = "MINIMODEM_ENCODER";


typedef struct minimodem_encoder
{
    minimodem_struct minimodem_str;
} minimodem_encoder_t;

static esp_err_t _minimodem_encoder_destroy(audio_element_handle_t self)
{
    minimodem_encoder_t *minimodem_enc = (minimodem_encoder_t *)audio_element_getdata(self);
    audio_free(minimodem_enc);
    return ESP_OK;
}
static esp_err_t _minimodem_encoder_open(audio_element_handle_t self)
{
    ESP_LOGD(TAG, "_minimodem_encoder_open");
    return ESP_OK;
}

static esp_err_t _minimodem_encoder_close(audio_element_handle_t self)
{
    ESP_LOGD(TAG, "_minimodem_encoder_close");
    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        audio_element_set_byte_pos(self, 0);
        audio_element_set_total_bytes(self, 0);
    }
    return ESP_OK;
}

static int _minimodem_encoder_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
	//fprintf(stderr, "Dbg1\n");
    minimodem_encoder_t *minimodem_enc = (minimodem_encoder_t *)audio_element_getdata(self);
    //fprintf(stderr, "Dbg2 %zu\n", minimodem_enc->minimodem_str.sample_rate);
    int r_size = audio_element_input(self, in_buffer, in_len);
    //fprintf(stderr, "Dbg3\n");
    int out_len = r_size;
    if (r_size > 0) {
        out_len = fsk_transmit_buf(minimodem_enc->minimodem_str, self, (unsigned char*)in_buffer, in_len);
        //audio_element_update_byte_pos(self, nbytes);
        fprintf(stderr, "Dbg4\n");
        //out_len = audio_element_output(self, in_buffer, r_size);
        if (out_len > 0) {
            audio_element_update_byte_pos(self, out_len);
        }
    }

	fprintf(stderr, "Dbg2\n");
    return out_len;
}

audio_element_handle_t minimodem_encoder_init(minimodem_encoder_cfg_t *config)
{
	fprintf(stderr, "here i1\n");
    minimodem_encoder_t *minimodem_enc = audio_calloc(1, sizeof(minimodem_encoder_t));
    AUDIO_MEM_CHECK(TAG, minimodem_enc, {return NULL;});
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.destroy = _minimodem_encoder_destroy;
    cfg.process = _minimodem_encoder_process;
    cfg.open = _minimodem_encoder_open;
    cfg.close = _minimodem_encoder_close;
    cfg.task_stack = MINIMODEM_ENCODER_TASK_STACK;
    if (config) {
        if (config->task_stack) {
            cfg.task_stack = config->task_stack;
        }
        cfg.stack_in_ext = config->stack_in_ext;
        cfg.task_prio = config->task_prio;
        cfg.task_core = config->task_core;
        cfg.out_rb_size = config->out_rb_size;
    }

    cfg.tag = "minimodem_enc";
    minimodem_enc->minimodem_str = config->minimodem_str;
    audio_element_handle_t el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, {audio_free(minimodem_enc); return NULL;});
    audio_element_setdata(el, minimodem_enc);
    ESP_LOGD(TAG, "minimodem_encoder_init");
    fprintf(stderr, "here i2\n");
    return el;
}
