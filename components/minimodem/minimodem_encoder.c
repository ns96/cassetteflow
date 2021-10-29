
#include "esp_log.h"
#include "audio_mem.h"
#include "audio_element.h"
#include "audio_error.h"

#include "minimodem_encoder.h"
#include "minimodem_config.h"

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

static audio_element_err_t _minimodem_encoder_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    minimodem_encoder_t *minimodem_enc = (minimodem_encoder_t *)audio_element_getdata(self);

    // consume input data line by line
    const int wanted_size = TAPEFILE_LINE_LENGTH + 1; // +1 line end character
    int r_size = audio_element_input(self, in_buffer, wanted_size);
    int out_len = r_size;
    if (r_size == wanted_size) {
        ESP_LOGI(TAG, "process: %29s", in_buffer);
        out_len = fsk_transmit_buf(&minimodem_enc->minimodem_str, self, in_buffer, wanted_size);
        //audio_element_update_byte_pos(self, nbytes);
        //out_len = audio_element_output(self, in_buffer, r_size);
        if (out_len > 0) {
            audio_element_update_byte_pos(self, out_len);
        }
    } else if (r_size > 0) {
        ESP_LOGW(TAG, "process: not enough data %d", r_size);
        out_len = AEL_IO_FAIL;
    }

    return out_len;
}

audio_element_handle_t minimodem_encoder_init(minimodem_encoder_cfg_t *config)
{
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

        minimodem_enc->minimodem_str = config->minimodem_str;
    }

    cfg.tag = "minimodem_enc";
    audio_element_handle_t el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, {audio_free(minimodem_enc); return NULL;});
    audio_element_setdata(el, minimodem_enc);
    ESP_LOGD(TAG, "minimodem_encoder_init");
    return el;
}
