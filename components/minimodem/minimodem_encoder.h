#ifndef _MINIMODEM_ENCODER_H_
#define _MINIMODEM_ENCODER_H_

#include "esp_err.h"
#include "audio_element.h"

#include "minimodem_enc_init.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief      Minimodem Encoder configurations
 */
typedef struct {
    int                     out_rb_size;    /*!< Size of output ringbuffer */
    int                     task_stack;     /*!< Task stack size */
    int                     task_core;      /*!< Task running in core (0 or 1) */
    int                     task_prio;      /*!< Task priority (based on freeRTOS priority) */
    bool                    stack_in_ext;   /*!< Try to allocate stack in external memory */
    minimodem_struct        minimodem_str;  /*!< Minimodem struct */ 
} minimodem_encoder_cfg_t;

#define MINIMODEM_ENCODER_TASK_STACK          (3 * 1024)
#define MINIMODEM_ENCODER_TASK_CORE           (0)
#define MINIMODEM_ENCODER_TASK_PRIO           (5)
#define MINIMODEM_ENCODER_RINGBUFFER_SIZE     (8 * 1024)

/*
#define DEFAULT_MINIMODEM_ENCODER_CONFIG() {\
    .out_rb_size        = MINIMODEM_ENCODER_RINGBUFFER_SIZE,\
    .task_stack         = MINIMODEM_ENCODER_TASK_STACK,\
    .task_core          = MINIMODEM_ENCODER_TASK_CORE,\
    .task_prio          = MINIMODEM_ENCODER_TASK_PRIO,\
    .stack_in_ext       = true,\
}
*/
#define DEFAULT_MINIMODEM_ENCODER_CONFIG() {\
    .out_rb_size        = MINIMODEM_ENCODER_RINGBUFFER_SIZE,\
    .task_stack         = MINIMODEM_ENCODER_TASK_STACK,\
    .task_core          = MINIMODEM_ENCODER_TASK_CORE,\
    .task_prio          = MINIMODEM_ENCODER_TASK_PRIO,\
    .stack_in_ext       = true,\
	.minimodem_str      = minimodem_transmit_cfg(), \
}

//.minimodem_str      = minimodem_transmit_cfg(4, (char*[]){"minimodem", "300", "-R", "48000"}),


/**
 * @brief      Create a handle to an Audio Element to encode incoming data using minimodem
 *
 * @param      config  The configuration
 *
 * @return     The audio element handle
 */
audio_element_handle_t minimodem_encoder_init(minimodem_encoder_cfg_t *config);


#ifdef __cplusplus
}
#endif

#endif

