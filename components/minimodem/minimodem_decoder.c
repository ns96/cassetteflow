
#include "esp_log.h"
#include "audio_mem.h"
#include "audio_element.h"
#include "minimodem_decoder.h"
#include "audio_error.h"

static const char *TAG = "MINIMODEM_DECODER";

typedef struct minimodem_encoder
{
    minimodem_decoder_struct *minimodem_str;
} minimodem_decoder_t;

static esp_err_t _minimodem_decoder_destroy(audio_element_handle_t self)
{
    minimodem_decoder_t *minimodem_dec = (minimodem_decoder_t *)audio_element_getdata(self);
    audio_free(minimodem_dec);
    return ESP_OK;
}
static esp_err_t _minimodem_decoder_open(audio_element_handle_t self)
{
    ESP_LOGD(TAG, "_minimodem_decoder_open");
    return ESP_OK;
}

static esp_err_t _minimodem_decoder_close(audio_element_handle_t self)
{
    ESP_LOGD(TAG, "_minimodem_decoder_close");
    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        audio_element_set_byte_pos(self, 0);
        audio_element_set_total_bytes(self, 0);
    }
    return ESP_OK;
}

static int _minimodem_decoder_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    minimodem_decoder_t *minimodem_dec = (minimodem_decoder_t *)audio_element_getdata(self);
    int r_size = audio_element_input(self, in_buffer, in_len);
    int out_len = 0;
    if (r_size > 0) {
        // TODO
        out_len = minimodem_dec_buf(minimodem_dec->minimodem_str, self, (unsigned char *)in_buffer, in_len);
        if (out_len > 0) {
            audio_element_update_byte_pos(self, out_len);
        }
    }
    return out_len;
}

audio_element_handle_t minimodem_decoder_init(minimodem_decoder_cfg_t *config)
{
    minimodem_decoder_t *minimodem_dec = audio_calloc(1, sizeof(minimodem_decoder_t));
    AUDIO_MEM_CHECK(TAG, minimodem_dec, { return NULL; });
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.destroy = _minimodem_decoder_destroy;
    cfg.process = _minimodem_decoder_process;
    cfg.open = _minimodem_decoder_open;
    cfg.close = _minimodem_decoder_close;
    cfg.task_stack = MINIMODEM_DECODER_TASK_STACK;
    if (config) {
        if (config->task_stack) {
            cfg.task_stack = config->task_stack;
        }
        cfg.stack_in_ext = config->stack_in_ext;
        cfg.task_prio = config->task_prio;
        cfg.task_core = config->task_core;
        cfg.out_rb_size = config->out_rb_size;

        minimodem_dec->minimodem_str = config->minimodem_str;
    }

    cfg.tag = "minimodem_dec";
    audio_element_handle_t el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, {
        audio_free(minimodem_dec);
        return NULL;
    });
    audio_element_setdata(el, minimodem_dec);
    ESP_LOGD(TAG, "minimodem_encoder_init");
    return el;
}
