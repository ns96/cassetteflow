#ifndef _MINIMODEM_ENC_INIT_H_
#define _MINIMODEM_ENC_INIT_H_

#include "databits.h"
#include "audio_element.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	float data_rate;
	size_t sample_rate;
	float bfsk_mark_f;
	float bfsk_space_f;
	int n_data_bits;
	float bfsk_nstartbits;
	float bfsk_nstopbits;
	int invert_start_stop;
	int bfsk_msb_first;
	unsigned int bfsk_do_tx_sync_bytes;
	unsigned int bfsk_sync_byte;
	int tx_leader_bits_len;
	int tx_trailer_bits_len;
	databits_encoder *encode;
	int txcarrier;
} minimodem_struct;

size_t fsk_transmit_buf
(
    minimodem_struct *s,
    audio_element_handle_t self,
    char *buf,
    size_t len
);

minimodem_struct minimodem_transmit_cfg(void);

#ifdef __cplusplus
}
#endif

#endif
