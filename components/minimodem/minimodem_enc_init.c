/*
 * minimodem.c
 *
 * minimodem - software audio Bell-type or RTTY FSK modem
 *
 * Copyright (C) 2011-2020 Kamal Mostafa <kamal@whence.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <getopt.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>

#include "databits.h"

#include "simple-tone-generator.h"

#include "audio_element.h"
#include "minimodem_enc_init.h"


static size_t fsk_transmit_frame(unsigned int bits, unsigned int n_data_bits,
		size_t bit_nsamples, float bfsk_mark_f, float bfsk_space_f,
		float bfsk_nstartbits, float bfsk_nstopbits, int invert_start_stop,
		int bfsk_msb_first, audio_element_handle_t self, size_t sample_rate);

static size_t fsk_transmit_char(minimodem_struct *s, audio_element_handle_t self, char buf);


/*
 * rudimentary BFSK transmitter
 */

static size_t fsk_transmit_frame(unsigned int bits, unsigned int n_data_bits,
		size_t bit_nsamples, float bfsk_mark_f, float bfsk_space_f,
		float bfsk_nstartbits, float bfsk_nstopbits, int invert_start_stop,
		int bfsk_msb_first, audio_element_handle_t self, size_t sample_rate) {
    int i;
    size_t out_len = 0;
    if (bfsk_nstartbits > 0) {
        size_t audio_len = simpleaudio_tone(
            invert_start_stop ? bfsk_mark_f : bfsk_space_f,
            bit_nsamples * bfsk_nstartbits, self, sample_rate);    // start
        if (audio_len == 0) {
            return 0;
        }
        out_len += audio_len;
    }
    for (i = 0; i < n_data_bits; i++) {                // data
        unsigned int bit;
        if (bfsk_msb_first) {
            bit = (bits >> (n_data_bits - i - 1)) & 1;
        } else {
            bit = (bits >> i) & 1;
        }

        float tone_freq = bit == 1 ? bfsk_mark_f : bfsk_space_f;
        size_t audio_len = simpleaudio_tone(tone_freq, bit_nsamples, self, sample_rate);
        if (audio_len == 0) {
            return 0;
        }
        out_len += audio_len;
    }
    if (bfsk_nstopbits > 0) {
        size_t audio_len = simpleaudio_tone(
            invert_start_stop ? bfsk_space_f : bfsk_mark_f,
            bit_nsamples * bfsk_nstopbits, self, sample_rate);        // stop
        if (audio_len == 0) {
            return 0;
        }
        out_len += audio_len;
    }
    return out_len;
}

static size_t fsk_transmit_char(minimodem_struct *s, audio_element_handle_t self,
                                char buf)
{
    size_t out_len = 0;
    const size_t bit_nsamples = s->sample_rate / s->data_rate + 0.5f;
    //static int idle = 0;
    static int tx_transmitting = 0;
    {
        // fprintf(stderr, "<c=%d>", c);
        unsigned int nwords;
        unsigned int bits[2];
        unsigned int j;
        nwords = s->encode(bits, buf);

		if (!tx_transmitting) {
            tx_transmitting = 1;
            /* emit leader tone (mark) */
            for (j = 0; j < s->tx_leader_bits_len; j++) {
                size_t audio_len = simpleaudio_tone(
                    s->invert_start_stop ? s->bfsk_space_f : s->bfsk_mark_f,
                    bit_nsamples, self, s->sample_rate);
                if (audio_len == 0) {
                    return 0;
                }
                out_len += audio_len;
            }
        }
		if (tx_transmitting < 2) {
            tx_transmitting = 2;
            /* emit "preamble" of sync bytes */
            for (j = 0; j < s->bfsk_do_tx_sync_bytes; j++) {
                size_t audio_len = fsk_transmit_frame(s->bfsk_sync_byte, s->n_data_bits,
                                                      bit_nsamples, s->bfsk_mark_f, s->bfsk_space_f,
                                                      s->bfsk_nstartbits, s->bfsk_nstopbits,
                                                      s->invert_start_stop, 0, self, s->sample_rate);
                if (audio_len == 0) {
                    return 0;
                }
                out_len += audio_len;
            }
        }

        /* emit data bits */
        for (j = 0; j < nwords; j++) {
            size_t audio_len = fsk_transmit_frame(bits[j], s->n_data_bits, bit_nsamples,
                                                  s->bfsk_mark_f, s->bfsk_space_f, s->bfsk_nstartbits,
                                                  s->bfsk_nstopbits, s->invert_start_stop, s->bfsk_msb_first,
                                                  self, s->sample_rate);
            if (audio_len == 0) {
                return 0;
            }
            out_len += audio_len;
        }
    }
	return out_len;
}

size_t fsk_transmit_buf(minimodem_struct *s, audio_element_handle_t self,
                        char *buf, size_t len)
{
    size_t out_len = 0;
    int pause_seconds = 0;

    // check for pause record
    // 0001A_03_b3488ae07e_000M_0481
    if (buf[23] == 'M') {
        sscanf(buf + 20, "%03dM_%*04d\n", &pause_seconds);
    }

    if (pause_seconds > 0) {
        for (int i = 0; i < pause_seconds; ++i) {
            // produce 1 second of silence
            size_t audio_len = simpleaudio_tone(0, s->sample_rate, self, s->sample_rate);
            if (audio_len == 0) {
                return 0;
            }
            out_len += audio_len;
        }
    } else {
        for (size_t i = 0; i < len; i++) {
            size_t audio_len = fsk_transmit_char(s, self, buf[i]);
            if (audio_len == 0) {
                return 0;
            }
            out_len += audio_len;
        }
    }

    return out_len;
}


minimodem_struct minimodem_transmit_cfg(void) {
	char *modem_mode = NULL;
	int TX_mode = 1;
	float band_width = 0;
	float bfsk_mark_f = 0;
	float bfsk_space_f = 0;
	unsigned int bfsk_inverted_freqs = 0;
	int bfsk_nstartbits = -1;
	float bfsk_nstopbits = -1;
	unsigned int bfsk_do_tx_sync_bytes = 0;
	unsigned long long bfsk_sync_byte = -1;
	unsigned int bfsk_n_data_bits = 0;
	int bfsk_msb_first = 0;
	int invert_start_stop = 0;
	int autodetect_shift;
	float carrier_autodetect_threshold = 0.0;
	float fsk_confidence_threshold = 1.5;
	float fsk_confidence_search_limit = 2.3f;
	unsigned int sample_rate;
	float tx_amplitude = 1.0;
	unsigned int tx_sin_table_len = 4096;
	int txcarrier = 0;
	int output_mode_raw_nbits = 0;

	float bfsk_data_rate = 0.0;
	databits_encoder *bfsk_databits_encode;

	bfsk_databits_encode = databits_encode_ascii8;

	int tx_leader_bits_len = 2;
	int tx_trailer_bits_len = 2;

	/////////////////////////////////////////////////////////////////
	/*
	 {baudmode}
	 any_number_N       Bell-like      N bps --ascii
	 1200       Bell202     1200 bps --ascii
	 300       Bell103      300 bps --ascii
	 rtty       RTTY       45.45 bps --baudot --stopbits=1.5
	 tdd       TTY/TDD    45.45 bps --baudot --stopbits=2.0
	 same       NOAA SAME 520.83 bps --sync-byte=0xAB ...
	 callerid       Bell202 CID 1200 bps
	 uic{-train,-ground}       UIC-751-3 Train/Ground 600 bps
	 */
	modem_mode = "1200";

	/*
	 -R, --samplerate {rate}
	*/
	// if  i2s_cfg.i2s_config.sample_rate = 48000;
	// then REAL sample rate = 48000*1.25 = 60000
	sample_rate = 48000;

    /////////////////////////////////////////////////////////////////
    // Use "minimodem 1200 -r" on PC to decode this
    /////////////////////////////////////////////////////////////////

	minimodem_struct mm = { .sample_rate = 0 }; // return this if error

	if (strncasecmp(modem_mode, "rtty", 5) == 0) {
		//bfsk_databits_decode = databits_decode_baudot;
		bfsk_databits_encode = databits_encode_baudot;
		bfsk_data_rate = 45.45;
		if (bfsk_n_data_bits == 0)
			bfsk_n_data_bits = 5;
		if (bfsk_nstopbits < 0)
			bfsk_nstopbits = 1.5;
	} else if (strncasecmp(modem_mode, "tdd", 4) == 0) {
		bfsk_databits_encode = databits_encode_baudot;
		bfsk_data_rate = 45.45;
		if (bfsk_n_data_bits == 0)
			bfsk_n_data_bits = 5;
		if (bfsk_nstopbits < 0)
			bfsk_nstopbits = 2.0;
		bfsk_mark_f = 1400;
		bfsk_space_f = 1800;
	} else if (strncasecmp(modem_mode, "same", 5) == 0) {
		bfsk_data_rate = 520.0 + 5 / 6.0;
		bfsk_n_data_bits = 8;
		bfsk_nstartbits = 0;
		bfsk_nstopbits = 0;
		bfsk_do_tx_sync_bytes = 16;
		bfsk_sync_byte = 0xAB;
		bfsk_mark_f = 2083.0 + 1 / 3.0;
		bfsk_space_f = 1562.5;
		band_width = bfsk_data_rate;
	} else if (strncasecmp(modem_mode, "caller", 6) == 0) {
		if (TX_mode) {
			fprintf(stderr, "E: callerid --tx mode is not supported.\n");
			return mm;
		}
		if (carrier_autodetect_threshold > 0.0f)
			fprintf(stderr,
					"W: callerid with --auto-carrier is not recommended.\n");
		//bfsk_databits_decode = databits_decode_callerid;
		bfsk_data_rate = 1200;
		bfsk_n_data_bits = 8;
	} else if (strncasecmp(modem_mode, "uic", 3) == 0) {
		if (TX_mode) {
			fprintf(stderr, "E: uic-751-3 --tx mode is not supported.\n");
			return mm;
		}
		// http://ec.europa.eu/transport/rail/interoperability/doc/ccs-tsi-en-annex.pdf
		//if (tolower(modem_mode[4]) == 't')
		//	bfsk_databits_decode = databits_decode_uic_train;
		//else
		//	bfsk_databits_decode = databits_decode_uic_ground;
		bfsk_data_rate = 600;
		bfsk_n_data_bits = 39;
		bfsk_mark_f = 1300;
		bfsk_space_f = 1700;
		bfsk_nstartbits = 8;
		bfsk_nstopbits = 0;
	} else if (strncasecmp(modem_mode, "V.21", 4) == 0) {
		bfsk_data_rate = 300;
		bfsk_mark_f = 980;
		bfsk_space_f = 1180;
		bfsk_n_data_bits = 8;
	} else {
		bfsk_data_rate = atof(modem_mode);
		if (bfsk_n_data_bits == 0)
			bfsk_n_data_bits = 8;
	}
	if (bfsk_data_rate == 0.0f)
		return mm;

	//if (output_mode_binary || output_mode_raw_nbits)
	//	bfsk_databits_decode = databits_decode_binary;

	if (output_mode_raw_nbits) {
		bfsk_nstartbits = 0;
		bfsk_nstopbits = 0;
		bfsk_n_data_bits = output_mode_raw_nbits;
	}

	if (bfsk_data_rate >= 400) {
		/*
		 * Bell 202:     baud=1200 mark=1200 space=2200
		 */
		autodetect_shift = -(bfsk_data_rate * 5 / 6);
		if (bfsk_mark_f == 0)
			bfsk_mark_f = bfsk_data_rate / 2 + 600;
		if (bfsk_space_f == 0)
			bfsk_space_f = bfsk_mark_f - autodetect_shift;
		if (band_width == 0)
			band_width = 200;
	} else if (bfsk_data_rate >= 100) {
		/*
		 * Bell 103:     baud=300 mark=1270 space=1070
		 */
		autodetect_shift = 200;
		if (bfsk_mark_f == 0)
			bfsk_mark_f = 1270;
		if (bfsk_space_f == 0)
			bfsk_space_f = bfsk_mark_f - autodetect_shift;
		if (band_width == 0)
			band_width = 50;	// close enough
	} else {
		/*
		 * RTTY:     baud=45.45 mark/space=variable shift=-170
		 */
		autodetect_shift = 170;
		if (bfsk_mark_f == 0)
			bfsk_mark_f = 1585;
		if (bfsk_space_f == 0)
			bfsk_space_f = bfsk_mark_f - autodetect_shift;
		if (band_width == 0) {
			band_width = 10;	// FIXME chosen arbitrarily
		}
	}

	// defaults: 1 start bit, 1 stop bit
	if (bfsk_nstartbits < 0)
		bfsk_nstartbits = 1;
	if (bfsk_nstopbits < 0)
		bfsk_nstopbits = 1.0;

	// n databits plus bfsk_startbit start bits plus bfsk_nstopbit stop bits:
	unsigned int bfsk_frame_n_bits = bfsk_n_data_bits + bfsk_nstartbits
			+ bfsk_nstopbits;
	if (bfsk_frame_n_bits > 64) {
		fprintf(stderr, "E: total number of bits per frame must be <= 64.\n");
		return mm;
	}

	// do not transmit any leader tone if no start bits
	if (bfsk_nstartbits == 0)
		tx_leader_bits_len = 0;

	if (bfsk_inverted_freqs) {
		float t = bfsk_mark_f;
		bfsk_mark_f = bfsk_space_f;
		bfsk_space_f = t;
	}

	/* restrict band_width to <= data rate (FIXME?) */
	if (band_width > bfsk_data_rate)
		band_width = bfsk_data_rate;

	// sanitize confidence search limit
	if (fsk_confidence_search_limit < fsk_confidence_threshold)
		fsk_confidence_search_limit = fsk_confidence_threshold;

	/*
	 * Handle transmit mode
	 */

	if (TX_mode)
	{
		simpleaudio_tone_init(tx_sin_table_len, tx_amplitude);

		return (minimodem_struct )
		{
			.data_rate = bfsk_data_rate,
			.sample_rate = sample_rate,
			.bfsk_mark_f = bfsk_mark_f,
			.bfsk_space_f = bfsk_space_f,
			.n_data_bits =	bfsk_n_data_bits,
			.bfsk_nstartbits =	bfsk_nstartbits,
			.bfsk_nstopbits = bfsk_nstopbits,
			.invert_start_stop = invert_start_stop,
			.bfsk_msb_first = bfsk_msb_first,
			.bfsk_do_tx_sync_bytes = bfsk_do_tx_sync_bytes,
			.bfsk_sync_byte = bfsk_sync_byte,
			.tx_leader_bits_len = tx_leader_bits_len,
			.tx_trailer_bits_len = tx_trailer_bits_len,
			.encode =	bfsk_databits_encode,
			.txcarrier = txcarrier,
		};
	}
	return mm;
}
