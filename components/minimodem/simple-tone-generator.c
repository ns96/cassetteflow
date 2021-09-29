/*
 * simple-tone-generator.c
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


#include <math.h>
#include <strings.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "simple-tone-generator.h"

#include "audio_mem.h"
#include "audio_element.h"

static float tone_mag = 1.0;

static unsigned int sin_table_len;
static short *sin_table_short;
//static float *sin_table_float;


static size_t esp32_write(void *buf, size_t nframes, audio_element_handle_t self);

void simpleaudio_tone_init(unsigned int new_sin_table_len, float mag)
{
    sin_table_len = new_sin_table_len;
    tone_mag = mag;

    if (sin_table_len != 0) {
        sin_table_short = realloc(sin_table_short, sin_table_len * sizeof(short));
        if (!sin_table_short) {
            perror("malloc");
            assert(0);
        }

        unsigned int i;
        unsigned short mag_s = 32767.0f * tone_mag + 0.5f;
        if (tone_mag > 1.0f) // clamp to 1.0 to avoid overflow
            mag_s = 32767;
        if (mag_s < 1) // "short epsilon"
            mag_s = 1;
        for (i = 0; i < sin_table_len; i++) {
            unsigned short a = lroundf(mag_s * sinf((float)M_PI * 2 * i / sin_table_len));
            sin_table_short[i] = ((signed short)a);
        }

    } else {
        if (sin_table_short) {
            free(sin_table_short);
            sin_table_short = NULL;
        }
    }
}

/*
 * in: turns (0.0 to 1.0)    out: (-32767 to +32767)
 */
static inline short
sin_lu_short(float turns)
{
    int t = (float)sin_table_len * turns + 0.5f;
    t %= sin_table_len;
    return sin_table_short[t];
}

/* "current" phase state of the tone generator */
static float sa_tone_cphase = 0.0;

void simpleaudio_tone_reset()
{
    sa_tone_cphase = 0.0;
}

size_t simpleaudio_tone(float tone_freq, size_t nsamples_dur, audio_element_handle_t self, unsigned int rate)
{
    const unsigned int framesize = 2; // 2 bytes per sample S16LE
    void *buf = malloc(nsamples_dur * framesize * 2); // "* 2" because this is stereo
    bzero(buf, nsamples_dur * framesize * 2);
    assert(buf);

    if (tone_freq != 0) {

        float wave_nsamples = rate / tone_freq;
        size_t i;

#define TURNS_TO_RADIANS(t)    ( (float)M_PI*2 * (t) )

#define SINE_PHASE_TURNS    ( (float)i/wave_nsamples + sa_tone_cphase )
#define SINE_PHASE_RADIANS    TURNS_TO_RADIANS(SINE_PHASE_TURNS)

        { // SA_SAMPLE_FORMAT_S16:

            short *short_buf = buf;
            if (sin_table_short) {
                for (i = 0; i < nsamples_dur; i++)
                    short_buf[i * 2 + 1] = short_buf[i * 2] = sin_lu_short(SINE_PHASE_TURNS); // left and right ch
            } else {
                unsigned short mag_s = 32767.0f * tone_mag + 0.5f;
                if (tone_mag > 1.0f) // clamp to 1.0 to avoid overflow
                    mag_s = 32767;
                if (mag_s < 1) // "short epsilon"
                    mag_s = 1;
                for (i = 0; i < nsamples_dur; i++)
                    short_buf[i * 2 + 1] = short_buf[i * 2] =
                        lroundf(mag_s * sinf(SINE_PHASE_RADIANS));  // left and right ch
            }
        }

        sa_tone_cphase
            = fmodf(sa_tone_cphase + (float)nsamples_dur / wave_nsamples, 1.0);

    } else {

        bzero(buf, nsamples_dur * framesize * 2);
        sa_tone_cphase = 0.0;

    }
    size_t out_len = esp32_write(buf, nsamples_dur, self);
    //assert ( out_len > 0 );

    free(buf);
    return out_len;
}

static size_t esp32_write(void *buf, size_t nframes, audio_element_handle_t self)
{
    const unsigned int framesize = 2; // 2 bytes per sample S16LE
    size_t nbytes = nframes * framesize * 2; // * 2 because stereo
    if (audio_element_output(self, (char *)buf, nbytes) != nbytes) {
        fprintf(stderr, "esp32_write error!\n");
        return -1;
    }
    //audio_element_update_byte_pos(self, nbytes);
    return nbytes;
}
