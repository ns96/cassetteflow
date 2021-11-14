#ifndef MINIMODEM_DEC_INIT_H_
#define MINIMODEM_DEC_INIT_H_

#include "databits.h"
#include "audio_element.h"
#include "fsk.h"
#include <stddef.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    unsigned int advance;
    size_t samplebuf_size;
    float *samplebuf;
    size_t samples_nvalid;
    float carrier_autodetect_threshold;
    unsigned int sample_rate;
    float bfsk_data_rate;
    unsigned int bfsk_inverted_freqs;
    int autodetect_shift;
    unsigned int expect_n_bits;
    float fsk_confidence_search_limit;
    char *expect_data_string;
    char *expect_sync_string;
    float fsk_confidence_threshold;
    unsigned int bfsk_n_data_bits;
    int bfsk_nstartbits;
    float bfsk_nstopbits;
    int bfsk_msb_first;
    unsigned int bfsk_do_rx_sync;
    unsigned int bfsk_sync_byte;
    int carrier;
    int carrier_band;
    float track_amplitude;
    float peak_confidence;
    unsigned int nframes_decoded;
    size_t carrier_nsamples;
    float confidence_total;
    float amplitude_total;
    unsigned int noconfidence;
    databits_decoder *bfsk_databits_decode;
    fsk_plan *fskp;
    char *buf;
    char *buf_part;
    size_t buf_part_pos;
    // size of buf_part
    size_t buf_load;
} minimodem_decoder_struct;

minimodem_decoder_struct *minimodem_receive_cfg();

audio_element_err_t minimodem_dec_buf(minimodem_decoder_struct *str,
                         audio_element_handle_t self, unsigned char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
