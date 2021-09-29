#ifndef _SIMPLE_TONE_GENERATOR_H_
#define _SIMPLE_TONE_GENERATOR_H_

#include "audio_element.h"

void simpleaudio_tone_reset();

size_t simpleaudio_tone(float tone_freq, size_t nsamples_dur, audio_element_handle_t self, unsigned int rate);

void simpleaudio_tone_init(unsigned int new_sin_table_len, float mag);

#endif
