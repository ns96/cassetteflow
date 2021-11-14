#ifndef MINIMODEM_DECODER_H_
#define MINIMODEM_DECODER_H_

#include "esp_err.h"
#include "audio_element.h"

#include "minimodem_dec_init.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief      Minimodem Decoder configurations
 */
typedef struct
{
    int out_rb_size;    /*!< Size of output ringbuffer */
    int task_stack;     /*!< Task stack size */
    int task_core;      /*!< Task running in core (0 or 1) */
    int task_prio;      /*!< Task priority (based on freeRTOS priority) */
    bool stack_in_ext;   /*!< Try to allocate stack in external memory */
    minimodem_decoder_struct *minimodem_str;  /*!< Minimodem struct */
} minimodem_decoder_cfg_t;

#define MINIMODEM_DECODER_TASK_STACK          (8 * 1024)
#define MINIMODEM_DECODER_TASK_CORE           (0)
#define MINIMODEM_DECODER_TASK_PRIO           (5)
#define MINIMODEM_DECODER_RINGBUFFER_SIZE     (8 * 1024)

#define DEFAULT_MINIMODEM_DECODER_CONFIG() {\
    .out_rb_size        = MINIMODEM_DECODER_RINGBUFFER_SIZE,\
    .task_stack         = MINIMODEM_DECODER_TASK_STACK,\
    .task_core          = MINIMODEM_DECODER_TASK_CORE,\
    .task_prio          = MINIMODEM_DECODER_TASK_PRIO,\
    .stack_in_ext       = false,\
    .minimodem_str      = minimodem_receive_cfg(), \
}

/**
 * @brief      Create a handle to an Audio Element to encode incoming data using minimodem
 *
 * @param      config  The configuration
 *
 * @return     The audio element handle
 */
audio_element_handle_t minimodem_decoder_init(minimodem_decoder_cfg_t *config);

#ifdef __cplusplus
}
#endif

#endif

