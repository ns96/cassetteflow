//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com> on 12.10.2021.
//

#include <string.h>
#include "mp3info.h"

// ported from https://github.com/mk-j/PHP_MP3_Duration/blob/master/mp3file.class.php

static const int16_t versions[4] = {
    25, //MPEG25
    0,  //NOT USED
    2,  //MPEG2
    1   //MPEG1
};

static int16_t layers[4] = {
    0,      //NOT USED
    3,      //Layer3
    2,      //Layer2
    1       //Layer1
};

static const int16_t bitrates[6][15] = {
    {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448}, //V1L1
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},     //V1L2
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},      //V1L3
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},      //V2L1
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},             // V2L2
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}             //V2L3
};

static const int sample_rates[3][3] = {
    {44100, 48000, 32000},  //MPEG1
    {22050, 24000, 16000},      //MPEG2
    {11025, 12000, 8000}      //MPEG2.5
};

static const int16_t samples_table[2][3] = {
    {384, 1152, 1152}, //MPEGv1,     Layers 1,2,3
    {384, 1152, 576} //MPEGv2/2.5, Layers 1,2,3
};

static int skipID3v2Tag(uint8_t *block, int length)
{
    if (strncmp((char *)block, "ID3", 3) == 0) {
        //found id3tag
        uint8_t id3v2_major_version = block[3];
        uint8_t id3v2_minor_version = block[4];
        uint8_t id3v2_flags = block[5];
        uint8_t flag_unsynchronisation = id3v2_flags & 0x80 ? 1 : 0;
        uint8_t flag_extended_header = id3v2_flags & 0x40 ? 1 : 0;
        uint8_t flag_experimental_ind = id3v2_flags & 0x20 ? 1 : 0;
        uint8_t flag_footer_present = id3v2_flags & 0x10 ? 1 : 0;
        uint8_t z0 = block[6];
        uint8_t z1 = block[7];
        uint8_t z2 = block[8];
        uint8_t z3 = block[9];
        if (((z0 & 0x80) == 0) && ((z1 & 0x80) == 0) && ((z2 & 0x80) == 0) && ((z3 & 0x80) == 0)) {
            int header_size = 10;
            int tag_size = ((z0 & 0x7f) * 2097152) + ((z1 & 0x7f) * 16384) + ((z2 & 0x7f) * 128) + (z3 & 0x7f);
            int footer_size = flag_footer_present ? 10 : 0;
            return header_size + tag_size + footer_size;//bytes to skip
        }
    }
    return 0;
}

static int frame_size(int16_t layer, int bitrate, int sample_rate, uint8_t padding_bit)
{
    if (layer == 1)
        return ((12 * bitrate * 1000 / sample_rate) + padding_bit) * 4;
    else //layer 2, 3
        return ((144 * bitrate * 1000) / sample_rate) + padding_bit;
}

static void parseFrameHeader(const uint8_t *block, int *framesize, int *samples, int *sample_rate)
{
    //uint8_t b0=block[0];//will always be 0xff
    uint8_t b1 = block[1];
    uint8_t b2 = block[2];
    uint8_t b3 = block[3];

    uint8_t version_bits = (b1 & 0x18) >> 3;
    int16_t version = versions[version_bits]; // MPEGVersion
    int16_t simple_version = 0;

    if (version == 25) {
        simple_version = 2;
    } else {
        simple_version = version;
    }

    if (version == 0) {
        return;
    }
    uint8_t layer_bits = (b1 & 0x06) >> 1;
    int16_t layer = layers[layer_bits];
    if (layer == 0) {
        return;
    }

    uint8_t protection_bit = (b1 & 0x01); //0=> protected by 2 byte CRC, 1=>not protected
    //bitrate_key = sprintf('V%dL%d', simple_version , layer);
    int16_t bitrate_key = (simple_version - 1) * 3 + (layer - 1);
    uint8_t bitrate_idx = (b2 & 0xf0) >> 4;
    int bitrate = bitrates[bitrate_key][bitrate_idx];

    uint8_t sample_rate_idx = (b2 & 0x0c) >> 2;//0xc => b1100
    *sample_rate = sample_rates[version - 1][sample_rate_idx];
    uint8_t padding_bit = (b2 & 0x02) >> 1;
    uint8_t private_bit = (b2 & 0x01);
    uint8_t channel_mode_bits = (b3 & 0xc0) >> 6;
    uint8_t mode_extension_bits = (b3 & 0x30) >> 4;
    uint8_t copyright_bit = (b3 & 0x08) >> 3;
    uint8_t original_bit = (b3 & 0x04) >> 2;
    uint8_t emphasis = (b3 & 0x03);

    *framesize = frame_size(layer, bitrate, *sample_rate, padding_bit);
    *samples = samples_table[simple_version - 1][layer - 1];
}

/**
 * Calculate mp3 file duration in seconds
 * @param file, File must be opened in 'rb' mode
 * @return duration in seconds
 */
int mp3info_get_duration(const char *filepath)
{
    FILE *file;
    float duration = 0;
    uint8_t block[10];

    file = fopen(filepath, "rb");
    if (file == NULL) {
        return 0;
    }

    if (fread(block, 1, sizeof(block), file) != sizeof(block)) {
        //read file error
        fclose(file);
        return 0;
    }

    int offset = skipID3v2Tag(block, sizeof(block));

    fseek(file, offset, SEEK_SET);

    while (!feof(file)) {
        if (fread(block, 1, sizeof(block), file) == sizeof(block)) {
            //looking for 1111 1111 111 (frame synchronization bits)
            if (block[0] == 0xff && block[1] & 0xe0) {
                int samples = 0, framesize = 0, sampling_rate = 0;
                parseFrameHeader(block, &framesize, &samples, &sampling_rate);
                fseek(file, framesize - 10, SEEK_CUR);
                if ((samples > 0) && (sampling_rate > 0)) {
                    duration += ((float)samples / sampling_rate);
                }
            } else if (strncmp((char *)&block, "TAG", 3) == 0) {
                //found idv3.1 tag, skip it
                fseek(file, 128 - 10, SEEK_CUR);//skip over id3v1 tag size
            } else {
                fseek(file, -9, SEEK_CUR);
            }
        }
    }

    fclose(file);
    return (int)duration;
}
