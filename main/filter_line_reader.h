//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 29.10.2021.
//

#ifndef CASSETTEFLOW_FIRMWARE_MAIN_FILTER_LINE_READER_H
#define CASSETTEFLOW_FIRMWARE_MAIN_FILTER_LINE_READER_H

typedef struct {
    int                     out_rb_size;    /*!< Size of output ringbuffer */
    int                     task_stack;     /*!< Task stack size */
    int                     task_core;      /*!< Task running in core (0 or 1) */
    int                     task_prio;      /*!< Task priority (based on freeRTOS priority) */
    bool                    stack_in_ext;   /*!< Try to allocate stack in external memory */
} filter_line_cfg_t;

#define FILTER_LINE_TASK_STACK          (3 * 1024)
#define FILTER_LINE_TASK_CORE           (0)
#define FILTER_LINE_TASK_PRIO           (5)
#define FILTER_LINE_RINGBUFFER_SIZE     (8 * 1024)

#define DEFAULT_FILTER_LINE_CONFIG() {\
    .out_rb_size        = FILTER_LINE_RINGBUFFER_SIZE,\
    .task_stack         = FILTER_LINE_TASK_STACK,\
    .task_core          = FILTER_LINE_TASK_CORE,\
    .task_prio          = FILTER_LINE_TASK_PRIO,\
    .stack_in_ext       = false, \
}

audio_element_handle_t filter_line_reader_init(filter_line_cfg_t *config);

#endif //CASSETTEFLOW_FIRMWARE_MAIN_FILTER_LINE_READER_H
