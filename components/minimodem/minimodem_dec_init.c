#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include <unistd.h>

#include "fsk.h"
#include "databits.h"

#include "minimodem_dec_init.h"

static float audio_sample_to_float(int16_t i);

static size_t esp32_write_b(void *buf, size_t bytes, audio_element_handle_t self);

size_t minimodem_decode(minimodem_decoder_struct *dec_str, audio_element_handle_t self);

ssize_t samples_read(void *buf, size_t nframes, char *in_buf);
//inline float audio_sample_to_float(int16_t i);

static void
report_no_carrier(fsk_plan *fskp, unsigned int sample_rate,
                  float bfsk_data_rate, float frame_n_bits, unsigned int nframes_decoded,
                  size_t carrier_nsamples, float confidence_total, float amplitude_total);

minimodem_decoder_struct *minimodem_receive_cfg();

static int
build_expect_bits_string(char *expect_bits_string, int bfsk_nstartbits,
                         int bfsk_n_data_bits, float bfsk_nstopbits, int invert_start_stop,
                         int use_expect_bits, unsigned long long expect_bits);

static float audio_sample_to_float(int16_t i)
{
    return ((float)i) / (float)32768;
}

static int build_expect_bits_string(char *expect_bits_string,
                                    int bfsk_nstartbits, int bfsk_n_data_bits, float bfsk_nstopbits,
                                    int invert_start_stop, int use_expect_bits,
                                    unsigned long long expect_bits)
{
    // example expect_bits_string
    //	  0123456789A
    //	  isddddddddp	i == idle bit (a.k.a. prev_stop bit)
    //			s == start bit  d == data bits  p == stop bit
    // ebs = "10dddddddd1"  <-- expected mark/space framing pattern
    //
    // NOTE! expect_n_bits ends up being (frame_n_bits+1), because
    // we expect the prev_stop bit in addition to this frame's own
    // (start + n_data_bits + stop) bits.  But for each decoded frame,
    // we will advance just frame_n_bits worth of samples, leaving us
    // pointing at our stop bit -- it becomes the next frame's prev_stop.
    //
    //                  prev_stop--v
    //                       start--v        v--stop
    // char *expect_bits_string = "10dddddddd1";
    //
    char start_bit_value = invert_start_stop ? '1' : '0';
    char stop_bit_value = invert_start_stop ? '0' : '1';
    int j = 0;
    if (bfsk_nstopbits != 0.0f)
        expect_bits_string[j++] = stop_bit_value;
    int i;
    // Nb. only integer number of start bits works (for rx)
    for (i = 0; i < bfsk_nstartbits; i++)
        expect_bits_string[j++] = start_bit_value;
    for (i = 0; i < bfsk_n_data_bits; i++, j++) {
        if (use_expect_bits)
            expect_bits_string[j] = ((expect_bits >> i) & 1) + '0';
        else
            expect_bits_string[j] = 'd';
    }
    if (bfsk_nstopbits != 0.0f)
        expect_bits_string[j++] = stop_bit_value;
    expect_bits_string[j] = 0;

    return j;
}

size_t minimodem_dec_buf(minimodem_decoder_struct *str, audio_element_handle_t self,
                         unsigned char *buf, size_t len)
{
    size_t out_len = 0;
    const size_t buf_load = (str->samplebuf_size / 2) * 2 * sizeof(int16_t);
    if (str->buf_part_pos != 0) {
        if (len + str->buf_part_pos > buf_load) {
            memcpy(str->buf_part + str->buf_part_pos, buf,
                   buf_load - str->buf_part_pos);
            buf += buf_load - str->buf_part_pos;
            str->buf = str->buf_part;
            out_len += minimodem_decode(str, self);
            len -= buf_load - str->buf_part_pos;
            str->buf_part_pos = 0;
        } else {
            memcpy(str->buf_part + str->buf_part_pos, buf, len);
            str->buf_part_pos += len;
            return 0;
        }
    }
    size_t i;
    for (i = 0; i + buf_load <= len; i += buf_load) {
        str->buf = (char *)buf + i;
        out_len += minimodem_decode(str, self);
    }
    if (i != len) {
        memcpy(str->buf_part, buf + i, len - i);
        str->buf_part_pos = len - i;
    }
    return out_len;
}

minimodem_decoder_struct *minimodem_receive_cfg()
{
    float band_width = 0;
    float bfsk_mark_f = 0;
    float bfsk_space_f = 0;
    unsigned int bfsk_inverted_freqs = 0;
    int bfsk_nstartbits = -1;
    float bfsk_nstopbits = -1;
    unsigned int bfsk_do_rx_sync = 0;
    unsigned long long bfsk_sync_byte = -1;
    unsigned int bfsk_n_data_bits = 0;
    int bfsk_msb_first = 0;
    char *expect_data_string = NULL;
    char *expect_sync_string = NULL;
    unsigned int expect_n_bits;
    int invert_start_stop = 0;
    int autodetect_shift;

    float carrier_autodetect_threshold = 0.0;

    // fsk_confidence_threshold : signal-to-noise squelch control
    //
    // The minimum SNR-ish confidence level seen as "a signal".
    float fsk_confidence_threshold = 1.5;

    // fsk_confidence_search_limit : performance vs. quality
    //
    // If we find a frame with confidence > confidence_search_limit,
    // quit searching for a better frame.  confidence_search_limit has a
    // dramatic effect on peformance (high value yields low performance, but
    // higher decode quality, for noisy or hard-to-discern signals (Bell 103,
    // or skewed rates).
    float fsk_confidence_search_limit = 2.3f;
    // float fsk_confidence_search_limit = INFINITY;  /* for test */

    //sa_backend_t sa_backend = SA_BACKEND_SYSDEFAULT;
    //char *sa_backend_device = NULL;
    //sa_format_t sample_format = SA_SAMPLE_FORMAT_S16;
    unsigned int sample_rate;

    // if i2s_cfg.i2s_config.sample_rate = 48000;
    // then REAL sample rate = 48000*1.25 = 60000
    sample_rate = 48000;

    int output_mode_binary = 0;
    int output_mode_raw_nbits = 0;

    float bfsk_data_rate = 0.0;
    databits_decoder *bfsk_databits_decode;

    bfsk_databits_decode = databits_decode_ascii8;
    char modem_mode[10] = "1200";
    // use "minimodem 1200 -t" to transmit data to device

    ////
    if (strncasecmp(modem_mode, "rtty", 5) == 0) {
        bfsk_databits_decode = databits_decode_baudot;
        bfsk_data_rate = 45.45;
        if (bfsk_n_data_bits == 0)
            bfsk_n_data_bits = 5;
        if (bfsk_nstopbits < 0)
            bfsk_nstopbits = 1.5;
    } else if (strncasecmp(modem_mode, "tdd", 4) == 0) {
        bfsk_databits_decode = databits_decode_baudot;
        bfsk_data_rate = 45.45;
        if (bfsk_n_data_bits == 0)
            bfsk_n_data_bits = 5;
        if (bfsk_nstopbits < 0)
            bfsk_nstopbits = 2.0;
        bfsk_mark_f = 1400;
        bfsk_space_f = 1800;
    } else if (strncasecmp(modem_mode, "same", 5) == 0) {
        // http://www.nws.noaa.gov/nwr/nwrsame.htm
        bfsk_data_rate = 520.0 + 5 / 6.0;
        bfsk_n_data_bits = 8;
        bfsk_nstartbits = 0;
        bfsk_nstopbits = 0;
        bfsk_do_rx_sync = 1;
        bfsk_sync_byte = 0xAB;
        bfsk_mark_f = 2083.0 + 1 / 3.0;
        bfsk_space_f = 1562.5;
        band_width = bfsk_data_rate;
    } else if (strncasecmp(modem_mode, "caller", 6) == 0) {
        if (carrier_autodetect_threshold > 0.0f)
            fprintf(stderr,
                    "W: callerid with --auto-carrier is not recommended.\n");
        bfsk_databits_decode = databits_decode_callerid;
        bfsk_data_rate = 1200;
        bfsk_n_data_bits = 8;
    } else if (strncasecmp(modem_mode, "uic", 3) == 0) {
        // http://ec.europa.eu/transport/rail/interoperability/doc/ccs-tsi-en-annex.pdf
        if (tolower(modem_mode[4]) == 't')
            bfsk_databits_decode = databits_decode_uic_train;
        else
            bfsk_databits_decode = databits_decode_uic_ground;
        bfsk_data_rate = 600;
        bfsk_n_data_bits = 39;
        bfsk_mark_f = 1300;
        bfsk_space_f = 1700;
        bfsk_nstartbits = 8;
        bfsk_nstopbits = 0;
        expect_data_string = "11110010ddddddddddddddddddddddddddddddddddddddd";
        expect_n_bits = 47;
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

    if (output_mode_binary || output_mode_raw_nbits)
        bfsk_databits_decode = databits_decode_binary;

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
            band_width = 50;    // close enough
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
            band_width = 10;    // FIXME chosen arbitrarily
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
        exit(1);
    }

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
     * Prepare the input sample chunk rate
     */
    float nsamples_per_bit = sample_rate / bfsk_data_rate;

    /*
     * Prepare the fsk plan
     */

    fsk_plan *fskp;
    fskp = fsk_plan_new(sample_rate, bfsk_mark_f, bfsk_space_f, band_width);
    if (!fskp) {
        fprintf(stderr, "fsk_plan_new() failed\n");
        return NULL;
    }
    /*
     * Prepare the input sample buffer.  For 8-bit frames with prev/start/stop
     * we need 11 data-bits worth of samples, and we will scan through one bits
     * worth at a time, hence we need a minimum total input buffer size of 12
     * data-bits.  */
    unsigned int nbits = 0;
    nbits += 1;            // prev stop bit (last whole stop bit)
    nbits += bfsk_nstartbits;    // start bits
    nbits += bfsk_n_data_bits;
    nbits += 1;            // stop bit (first whole stop bit)

    // FIXME EXPLAIN +1 goes with extra bit when scanning
    size_t samplebuf_size = ceilf(nsamples_per_bit) * (nbits + 1);
    samplebuf_size *= 2; // account for the half-buf filling method
#define SAMPLE_BUF_DIVISOR 12
#ifdef SAMPLE_BUF_DIVISOR
    // For performance, use a larger samplebuf_size than necessary
    if (samplebuf_size < sample_rate / SAMPLE_BUF_DIVISOR)
        samplebuf_size = sample_rate / SAMPLE_BUF_DIVISOR;
#endif
    float *samplebuf = malloc(samplebuf_size * sizeof(float));
    size_t samples_nvalid = 0;
    debug_log("samplebuf_size=%zu\n", samplebuf_size);

    /*
     * Run the main loop
     */
    int carrier = 0;
    float confidence_total = 0;
    float amplitude_total = 0;
    unsigned int nframes_decoded = 0;
    size_t carrier_nsamples = 0;

    unsigned int noconfidence = 0;
    unsigned int advance = 0;

    // Fraction of nsamples_per_bit that we will "overscan"; range (0.0 .. 1.0)
    float fsk_frame_overscan = 0.5;
    //   should be != 0.0 (only the nyquist edge cases actually require this?)
    // for handling of slightly faster-than-us rates:
    //   should be >> 0.0 to allow us to lag back for faster-than-us rates
    //   should be << 1.0 or we may lag backwards over whole bits
    // for optimal analysis:
    //   should be >= 0.5 (half a bit width) or we may not find the optimal bit
    //   should be <  1.0 (a full bit width) or we may skip over whole bits
    // for encodings without start/stop bits:
    //     MUST be <= 0.5 or we may accidentally skip a bit
    //
    assert(fsk_frame_overscan >= 0.0f && fsk_frame_overscan < 1.0f);

    // ensure that we overscan at least a single sample
    unsigned int nsamples_overscan = nsamples_per_bit * fsk_frame_overscan
        + 0.5f;
    if (fsk_frame_overscan > 0.0f && nsamples_overscan == 0)
        nsamples_overscan = 1;
    debug_log("fsk_frame_overscan=%f nsamples_overscan=%u\n",
              fsk_frame_overscan, nsamples_overscan);

    char expect_data_string_buffer[64];
    if (expect_data_string == NULL) {
        expect_data_string = expect_data_string_buffer;
        expect_n_bits = build_expect_bits_string(expect_data_string,
                                                 bfsk_nstartbits, bfsk_n_data_bits, bfsk_nstopbits,
                                                 invert_start_stop, 0, 0);
    } debug_log("eds = '%s' (%lu)\n", expect_data_string, strlen(expect_data_string));

    char expect_sync_string_buffer[64];
    if (expect_sync_string == NULL && bfsk_do_rx_sync
        && (long long)bfsk_sync_byte >= 0) {
        expect_sync_string = expect_sync_string_buffer;
        build_expect_bits_string(expect_sync_string, bfsk_nstartbits,
                                 bfsk_n_data_bits, bfsk_nstopbits, invert_start_stop, 1,
                                 bfsk_sync_byte);
    } else {
        expect_sync_string = expect_data_string;
    } debug_log("ess = '%s' (%lu)\n", expect_sync_string, strlen(expect_sync_string));

    //unsigned int expect_nsamples = nsamples_per_bit * expect_n_bits;
    float track_amplitude = 0.0;
    float peak_confidence = 0.0;

    minimodem_decoder_struct *ret = malloc(sizeof(minimodem_decoder_struct));
    if (ret == NULL)
        return NULL;
    char *buf_part = malloc((samplebuf_size / 2) * 2 * sizeof(int16_t));
    if (buf_part == NULL) {
        free(ret);
        return NULL;
    }
    *ret =
        (minimodem_decoder_struct)
            {.advance = advance, .samplebuf_size = samplebuf_size,
                .samplebuf = samplebuf, .samples_nvalid =
            samples_nvalid,
                .carrier_autodetect_threshold =
                carrier_autodetect_threshold, .sample_rate =
            sample_rate, .bfsk_data_rate =
            bfsk_data_rate, .bfsk_inverted_freqs =
            bfsk_inverted_freqs, .autodetect_shift =
            autodetect_shift, .expect_n_bits =
            expect_n_bits,
                .fsk_confidence_search_limit =
                fsk_confidence_search_limit,
                .expect_data_string = strdup(expect_data_string),
                .expect_sync_string = strdup(expect_sync_string),
                .fsk_confidence_threshold = fsk_confidence_threshold,
                .bfsk_n_data_bits = bfsk_n_data_bits,
                .bfsk_nstartbits = bfsk_nstartbits,
                .bfsk_nstopbits = bfsk_nstopbits, .bfsk_msb_first =
            bfsk_msb_first, .bfsk_do_rx_sync =
            bfsk_do_rx_sync, .bfsk_sync_byte =
            bfsk_sync_byte, .carrier = carrier,
                .carrier_band = -1, .track_amplitude =
            track_amplitude, .peak_confidence =
            peak_confidence, .nframes_decoded =
            nframes_decoded, .carrier_nsamples =
            carrier_nsamples, .confidence_total =
            confidence_total, .amplitude_total =
            amplitude_total, .noconfidence =
            noconfidence, .bfsk_databits_decode =
            bfsk_databits_decode, .fskp = fskp,
                .buf_part = buf_part, .buf_part_pos = 0};
    return ret;
}

// see https://github.com/kamalmostafa/minimodem/blob/bb2f34cf5148f101563aa926e201d306edbacbd3/src/minimodem.c#L1137
size_t minimodem_decode(minimodem_decoder_struct *dec_str, audio_element_handle_t self)
{
    const float nsamples_per_bit = dec_str->sample_rate
        / dec_str->bfsk_data_rate;
    const int quiet_mode = 1;
    const unsigned int bfsk_frame_n_bits = dec_str->bfsk_n_data_bits
        + dec_str->bfsk_nstartbits + dec_str->bfsk_nstopbits;
    const float frame_n_bits = bfsk_frame_n_bits;
    const unsigned int frame_nsamples = nsamples_per_bit * frame_n_bits + 0.5f;
    const float fsk_frame_overscan = 0.5;
    const unsigned int expect_nsamples = nsamples_per_bit
        * dec_str->expect_n_bits;
    const unsigned int nsamples_overscan = nsamples_per_bit * fsk_frame_overscan
        + 0.5f;
    int is_read = 0;
    size_t wr_bytes = 0;
    while (!is_read) {
        //if ( rx_stop )
        //    break;

        debug_log("dec_str->advance=%u\n", dec_str->advance);

        /* Shift the samples in samplebuf by 'dec_str->advance' samples */
        assert(dec_str->advance <= dec_str->samplebuf_size);
        if (dec_str->advance == dec_str->samplebuf_size) {
            dec_str->samples_nvalid = 0;
            dec_str->advance = 0;
        }
        if (dec_str->advance) {
            if (dec_str->advance > dec_str->samples_nvalid) {
                fprintf(stderr, "ERROR\n");
                return 0;
            }
            memmove(dec_str->samplebuf, dec_str->samplebuf + dec_str->advance,
                    (dec_str->samplebuf_size - dec_str->advance)
                        * sizeof(float));
            dec_str->samples_nvalid -= dec_str->advance;
        }

        if (dec_str->samples_nvalid < dec_str->samplebuf_size / 2) {
            float *samples_readptr = dec_str->samplebuf
                + dec_str->samples_nvalid;
            size_t read_nsamples = dec_str->samplebuf_size / 2;
            /* Read more samples into samplebuf (fill it) */
            assert(read_nsamples > 0);
            assert(
                dec_str->samples_nvalid + read_nsamples
                    <= dec_str->samplebuf_size);
            ssize_t r;
            r = samples_read(samples_readptr, read_nsamples, dec_str->buf);
            debug_log("samples_read(dec_str->samplebuf+%td, n=%zu) returns %zd\n",
                      samples_readptr - dec_str->samplebuf, read_nsamples, r);
            if (r < 0) {
                fprintf(stderr, "samples_read: error\n");
                //ret = -1;//float dec_str->fsk_confidence_threshold
                fprintf(stderr, "ERROR\n");
                return 0;
            }
            is_read = 1;
            dec_str->samples_nvalid += r;
        }

        if (dec_str->samples_nvalid == 0) {
            fprintf(stderr, "ERROR\n");
            return 0;
        }

        /* Auto-detect carrier frequency */
        //static int dec_str->carrier_band = -1;
        if (dec_str->carrier_autodetect_threshold > 0.0f
            && dec_str->carrier_band < 0) {
            unsigned int i;
            float nsamples_per_scan = nsamples_per_bit;
            if (nsamples_per_scan > dec_str->fskp->fftsize)
                nsamples_per_scan = dec_str->fskp->fftsize;
            for (i = 0; i + nsamples_per_scan <= dec_str->samples_nvalid; i +=
                                                                              nsamples_per_scan) {
                dec_str->carrier_band = fsk_detect_carrier(dec_str->fskp,
                                                           dec_str->samplebuf + i, nsamples_per_scan,
                                                           dec_str->carrier_autodetect_threshold);
                if (dec_str->carrier_band >= 0) {
                    fprintf(stderr, "ERROR\n");
                    return 0;
                }
            }
            dec_str->advance = i + nsamples_per_scan;
            if (dec_str->advance > dec_str->samples_nvalid)
                dec_str->advance = dec_str->samples_nvalid;
            if (dec_str->carrier_band < 0) {
                debug_log("autodetected carrier band not found\n");
                continue;
                //return 0;
            }

            // default negative shift -- reasonable?
            int b_shift = -(float)(dec_str->autodetect_shift
                + dec_str->fskp->band_width / 2.0f)
                / dec_str->fskp->band_width;
            if (dec_str->bfsk_inverted_freqs)
                b_shift *= -1;
            /* only accept a carrier as b_mark if it will not result
             * in a b_space band which is "too low". */
            int b_space = dec_str->carrier_band + b_shift;
            if (b_space < 1 || b_space >= dec_str->fskp->nbands) {
                debug_log("autodetected space band out of range\n");
                dec_str->carrier_band = -1;
                continue;
                //return 0;
            }

            debug_log("### TONE freq=%.1f ###\n",
                      dec_str->carrier_band * dec_str->fskp->band_width);

            fsk_set_tones_by_bandshift(dec_str->fskp, /*b_mark*/
                                       dec_str->carrier_band, b_shift);
        }

        /*
         * The main processing algorithm: scan samplesbuf for FSK frames,
         * looking at an entire frame at once.
         */

        debug_log("--------------------------\n");

        if (dec_str->samples_nvalid < expect_nsamples) {
            fprintf(stderr, "ERROR\n");
            return 0;
        }

        // try_max_nsamples
        // serves two purposes
        // 1. avoids finding a non-optimal first frame
        // 2. allows us to track slightly slow signals
        unsigned int try_max_nsamples;
        if (dec_str->carrier)
            try_max_nsamples = nsamples_per_bit * 0.75f + 0.5f;
        else
            try_max_nsamples = nsamples_per_bit;
        try_max_nsamples += nsamples_overscan;

        // FSK_ANALYZE_NSTEPS Try 3 frame positions across the try_max_nsamples
        // range.  Using a larger nsteps allows for more accurate tracking of
        // fast/slow signals (at decreased performance).  Note also
        // FSK_ANALYZE_NSTEPS_FINE below, which refines the frame
        // position upon first acquiring carrier, or if confidence falls.
#define FSK_ANALYZE_NSTEPS        3
        unsigned int try_step_nsamples = try_max_nsamples / FSK_ANALYZE_NSTEPS;
        if (try_step_nsamples == 0)
            try_step_nsamples = 1;

        float confidence, amplitude;
        unsigned long long bits = 0;
        /* Note: frame_start_sample is actually the sample where the
         * prev_stop bit begins (since the "frame" includes the prev_stop). */
        unsigned int frame_start_sample = 0;

        unsigned int try_first_sample;
        float try_confidence_search_limit;

        try_confidence_search_limit = dec_str->fsk_confidence_search_limit;
        try_first_sample = dec_str->carrier ? nsamples_overscan : 0;

        confidence = fsk_find_frame(dec_str->fskp, dec_str->samplebuf,
                                    expect_nsamples, try_first_sample, try_max_nsamples,
                                    try_step_nsamples, try_confidence_search_limit,
                                    dec_str->carrier ?
                                    dec_str->expect_data_string :
                                    dec_str->expect_sync_string, &bits, &amplitude,
                                    &frame_start_sample); // TODO dec_str->expect_data_string : dec_str->expect_sync_string

        int do_refine_frame = 0;

        if (confidence < dec_str->peak_confidence * 0.75f) {
            do_refine_frame = 1;
            debug_log(
                " ... do_refine_frame rescan (confidence %.3f << %.3f peak)\n",
                confidence, dec_str->peak_confidence);
            dec_str->peak_confidence = 0;
        }

        // no-confidence if amplitude drops abruptly to < 25% of the
        // dec_str->track_amplitude, which follows amplitude with hysteresis
        if (amplitude < dec_str->track_amplitude * 0.25f) {
            confidence = 0;
        }

#define FSK_MAX_NOCONFIDENCE_BITS    20

        if (confidence <= dec_str->fsk_confidence_threshold) {

            // FIXME: explain
            if (++dec_str->noconfidence > FSK_MAX_NOCONFIDENCE_BITS) {
                dec_str->carrier_band = -1;
                if (dec_str->carrier) {
                    if (!quiet_mode)
                        report_no_carrier(dec_str->fskp, dec_str->sample_rate,
                                          dec_str->bfsk_data_rate, frame_n_bits,
                                          dec_str->nframes_decoded,
                                          dec_str->carrier_nsamples,
                                          dec_str->confidence_total,
                                          dec_str->amplitude_total);
                    dec_str->carrier = 0;
                    dec_str->carrier_nsamples = 0;
                    dec_str->confidence_total = 0;
                    dec_str->amplitude_total = 0;
                    dec_str->nframes_decoded = 0;
                    dec_str->track_amplitude = 0.0;

                    //if (rx_one)
                    //	break;
                }
            }

            /* dec_str->advance the sample stream forward by try_max_nsamples so the
             * next time around the loop we continue searching from where
             * we left off this time.		*/
            dec_str->advance = try_max_nsamples;
            debug_log("@ NOCONFIDENCE=%u dec_str->advance=%u\n", dec_str->noconfidence, dec_str->advance);
            continue;
        }

        // Add a frame's worth of samples to the sample count
        dec_str->carrier_nsamples += frame_nsamples;

        if (dec_str->carrier) {

            // If we already had carrier, adjust sample count +start -overscan
            dec_str->carrier_nsamples += frame_start_sample;
            dec_str->carrier_nsamples -= nsamples_overscan;

        } else {

            // We just acquired carrier.

            if (!quiet_mode) {
                if (dec_str->bfsk_data_rate >= 100)
                    fprintf(stderr, "### CARRIER %u @ %.1f Hz ",
                            (unsigned int)(dec_str->bfsk_data_rate + 0.5f),
                            (double)(dec_str->fskp->b_mark
                                * dec_str->fskp->band_width));
                else
                    fprintf(stderr, "### CARRIER %.2f @ %.1f Hz ",
                            (double)(dec_str->bfsk_data_rate),
                            (double)(dec_str->fskp->b_mark
                                * dec_str->fskp->band_width));
            }

            if (!quiet_mode)
                fprintf(stderr, "###\n");

            dec_str->carrier = 1;
            dec_str->bfsk_databits_decode(0, 0, 0, 0); // reset the frame processor

            do_refine_frame = 1;
            debug_log(" ... do_refine_frame rescan (acquired carrier)\n");
        }

        if (do_refine_frame) {
            if (confidence < INFINITY && try_step_nsamples > 1) {
                // FSK_ANALYZE_NSTEPS_FINE:
                // Scan again, but try harder to find the best frame.
                // Since we found a valid confidence frame in the "sloppy"
                // fsk_find_frame() call already, we're sure to find one at
                // least as good this time.
#define FSK_ANALYZE_NSTEPS_FINE        8
                try_step_nsamples = try_max_nsamples / FSK_ANALYZE_NSTEPS_FINE;
                if (try_step_nsamples == 0)
                    try_step_nsamples = 1;
                try_confidence_search_limit = INFINITY;
                float confidence2, amplitude2;
                unsigned long long bits2;
                unsigned int frame_start_sample2;
                confidence2 = fsk_find_frame(dec_str->fskp, dec_str->samplebuf,
                                             expect_nsamples, try_first_sample, try_max_nsamples,
                                             try_step_nsamples, try_confidence_search_limit,
                                             dec_str->carrier ?
                                             dec_str->expect_data_string :
                                             dec_str->expect_sync_string, &bits2,
                                             &amplitude2, &frame_start_sample2);
                if (confidence2 > confidence) {
                    bits = bits2;
                    amplitude = amplitude2;
                    frame_start_sample = frame_start_sample2;
                }
            }
        }

        dec_str->track_amplitude = (dec_str->track_amplitude + amplitude) / 2;
        if (dec_str->peak_confidence < confidence)
            dec_str->peak_confidence = confidence;
        debug_log(
            "@ confidence=%.3f peak_conf=%.3f amplitude=%.3f dec_str->track_amplitude=%.3f\n",
            confidence, dec_str->peak_confidence, amplitude, dec_str->track_amplitude);

        dec_str->confidence_total += confidence;
        dec_str->amplitude_total += amplitude;
        dec_str->nframes_decoded++;
        dec_str->noconfidence = 0;

        // dec_str->advance the sample stream forward past the junk before the
        // frame starts (frame_start_sample), and then past decoded frame
        // (see also NOTE about frame_n_bits and dec_str->expect_n_bits)...
        // But actually dec_str->advance just a bit less than that to allow
        // for tracking slightly fast signals, hence - nsamples_overscan.
        dec_str->advance = frame_start_sample + frame_nsamples
            - nsamples_overscan;

        debug_log("@ nsamples_per_bit=%.3f n_data_bits=%u "
                  " frame_start=%u dec_str->advance=%u\n", nsamples_per_bit,
                  dec_str->bfsk_n_data_bits, frame_start_sample, dec_str->advance);

        // chop off the prev_stop bit
        if (dec_str->bfsk_nstopbits != 0.0f)
            bits = bits >> 1;

        /*
         * Send the raw data frame bits to the backend frame processor
         * for final conversion to output data bytes.
         */

        // chop off framing bits
        bits = bit_window(bits, dec_str->bfsk_nstartbits,
                          dec_str->bfsk_n_data_bits);
        if (dec_str->bfsk_msb_first) {
            bits = bit_reverse(bits, dec_str->bfsk_n_data_bits);
        } debug_log("Input: %08x%08x - Databits: %u - Shift: %i\n",
                    (unsigned int)(bits >> 32), (unsigned int)bits,
                    dec_str->bfsk_n_data_bits, dec_str->bfsk_nstartbits);

        unsigned int dataout_size = 4096;
        char dataoutbuf[4096];
        unsigned int dataout_nbytes = 0;

        // suppress printing of dec_str->bfsk_sync_byte bytes
        if (dec_str->bfsk_do_rx_sync) {
            if (dataout_nbytes == 0 && bits == dec_str->bfsk_sync_byte) {
                continue;
            }
        }

        dataout_nbytes += dec_str->bfsk_databits_decode(
            dataoutbuf + dataout_nbytes, dataout_size - dataout_nbytes,
            bits, (int)dec_str->bfsk_n_data_bits);

        if (dataout_nbytes == 0) {
            continue;
        }

        /*
         * Print the output buffer to stdout
         */
        // TODO
        // https://github.com/kamalmostafa/minimodem/blob/bb2f34cf5148f101563aa926e201d306edbacbd3/src/minimodem.c#L1451
//		if (write(1, dataoutbuf, dataout_nbytes) < 0)
//			perror("write");
        esp32_write_b(dataoutbuf, dataout_nbytes, self);
        wr_bytes += dataout_nbytes;
    }
    return wr_bytes;
}

static size_t esp32_write_b(void *buf, size_t bytes, audio_element_handle_t self)
{
    if (audio_element_output(self, (char *)buf, bytes) != bytes) {
        fprintf(stderr, "esp32_write error!\n");
        return -1;
    }
    return bytes;
}

// input is 16bit Little endian stereo (S16LE)
// and output is float mono
ssize_t samples_read(void *buf, size_t nframes, char *in_buf)
{
    typedef int16_t __attribute((__may_alias__)) int16_t_m_a;
    int16_t_m_a *tmp_buf = (int16_t_m_a *)in_buf;
    //const int val = (nframes * 2 * sizeof(int16_t));


    //if (val != (nframes * 2 * sizeof(int16_t))) {
    //	fprintf(stderr, "samples_read error!\n");
    //	exit(-1);
    //	return -1;
    //}
    float *out_fl = buf;
    for (size_t i = 0; i < nframes; ++i) {
        out_fl[i] = audio_sample_to_float(
            ((int32_t)tmp_buf[i * 2] + tmp_buf[i * 2 + 1]) / 2);
    }

    return nframes;
}

static void report_no_carrier(fsk_plan *fskp, unsigned int sample_rate,
                              float bfsk_data_rate, float frame_n_bits, unsigned int nframes_decoded,
                              size_t carrier_nsamples, float confidence_total, float amplitude_total)
{
    float nbits_decoded = nframes_decoded * frame_n_bits;
#if 0
    fprintf(stderr, "nframes_decoded=%u\n", nframes_decoded);
    fprintf(stderr, "nbits_decoded=%f\n", nbits_decoded);
    fprintf(stderr, "carrier_nsamples=%lu\n", carrier_nsamples);
#endif
    float throughput_rate = nbits_decoded * sample_rate
        / (float)carrier_nsamples;
    fprintf(stderr,
            "\n### NOCARRIER ndata=%u confidence=%.3f ampl=%.3f bps=%.2f",
            nframes_decoded, (double)(confidence_total / nframes_decoded),
            (double)(amplitude_total / nframes_decoded),
            (double)(throughput_rate));
#if 0
    fprintf(stderr, " bits*sr=%llu rate*nsamp=%llu",
        (unsigned long long)(nbits_decoded * sample_rate + 0.5),
        (unsigned long long)(bfsk_data_rate * carrier_nsamples) );
#endif
    if ((unsigned long long)(nbits_decoded * sample_rate + 0.5f)
        == (unsigned long long)(bfsk_data_rate * carrier_nsamples)) {
        fprintf(stderr, " (rate perfect) ###\n");
    } else {
        float throughput_skew = (throughput_rate - bfsk_data_rate)
            / bfsk_data_rate;
        fprintf(stderr, " (%.1f%% %s) ###\n",
                (double)(fabsf(throughput_skew) * 100.0f),
                signbit(throughput_skew) ? "slow" : "fast");
    }
}
