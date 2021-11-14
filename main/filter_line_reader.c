//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 29.10.2021.
//

#include "esp_log.h"
#include "audio_mem.h"
#include "audio_element.h"
#include "audio_error.h"

#include "filter_line_reader.h"

#define FILTER_MAX_LINE_LENGTH     (64)

static const char *TAG = "filter_line_reader";

typedef struct
{
    int line_length;
    char line[FILTER_MAX_LINE_LENGTH];
} filter_line_data_t;


static esp_err_t filter_line_reader_destroy(audio_element_handle_t self)
{
    ESP_LOGD(TAG, "filter_line_reader_destroy");
    filter_line_data_t *filter_line_data = (filter_line_data_t *)audio_element_getdata(self);
    audio_free(filter_line_data);
    return ESP_OK;
}
static esp_err_t filter_line_reader_open(audio_element_handle_t self)
{
    ESP_LOGD(TAG, "filter_line_reader_open");
    return ESP_OK;
}

static esp_err_t filter_line_reader_close(audio_element_handle_t self)
{
    ESP_LOGD(TAG, "filter_line_reader_close");
    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        audio_element_set_byte_pos(self, 0);
        audio_element_set_total_bytes(self, 0);
    }
    return ESP_OK;
}


static audio_element_err_t filter_line_reader_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    int out_len = in_len;
    int r_size;
    filter_line_data_t *data = (filter_line_data_t *)audio_element_getdata(self);

    // consume input 1 char at a time
    while (1) {
        r_size = audio_element_input(self, in_buffer, 1);
        if (r_size != 1) {
            // no data or error
            out_len = r_size;
            break;
        }
        char ch = in_buffer[0];
        if (ch == '\r') {
            // ignore CRs
            continue;
        }
        if (ch == '\n') {
            // end of line - output it
            data->line[data->line_length] = 0;
#if 0
            out_len = audio_element_output(self, filter_line_data->line, filter_line_data->line_length);
            if (out_len > 0) {
                audio_element_update_byte_pos(self, out_len);
            }
#else
            audio_element_set_uri(self, data->line);
            // send event to the main loop
            audio_element_report_pos(self);
#endif
            data->line_length = 0;
        } else {
            data->line[data->line_length] = ch;
            data->line_length++;
            if (data->line_length >= sizeof(data->line) - 1) {
                data->line_length = 0;
            }
        }
    };

    return out_len;
}

audio_element_handle_t filter_line_reader_init(filter_line_cfg_t *config)
{
    filter_line_data_t *filter_line_data = audio_calloc(1, sizeof(filter_line_data_t));
    AUDIO_MEM_CHECK(TAG, filter_line_data, {return NULL;});

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.destroy = filter_line_reader_destroy;
    cfg.process = filter_line_reader_process;
    cfg.open = filter_line_reader_open;
    cfg.close = filter_line_reader_close;
    cfg.task_stack = FILTER_LINE_TASK_STACK;
    if (config) {
        if (config->task_stack) {
            cfg.task_stack = config->task_stack;
        }
        cfg.stack_in_ext = config->stack_in_ext;
        cfg.task_prio = config->task_prio;
        cfg.task_core = config->task_core;
        cfg.out_rb_size = config->out_rb_size;
    }

    cfg.tag = "filter_line_reader";
    audio_element_handle_t el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, {audio_free(filter_line_data); return NULL;});
    audio_element_setdata(el, filter_line_data);
    ESP_LOGD(TAG, "filter_line_reader_init");
    return el;
}