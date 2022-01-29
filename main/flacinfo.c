//
// Created by Volodymyr Ananiev <volodymyr.ananiev@gmail.com>
//

#include "flacinfo.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

//https://github.com/devsnd/tinytag/blob/master/tinytag/tinytag.py

static int bytes_to_int(const uint8_t *data, int length)
{
    int result = 0;
    for(int i = 0; i < length; i++) {
        result = (result << 8) + data[i];
    }
    return result;
}

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

/**
 * Calculate flac file duration in seconds and sampling_rate
 * @param filepath, File must be opened in 'rb' mode
 * @param duration duration in seconds
 * @param avg_bitrate average bitrate (bits per seconds)
 * @return 0 - OK, -1 - ERROR
 */
int flacinfo_get_info(const char *filepath, int *duration, int *avg_bitrate)
{
    FILE *file;
    float f_duration = 0;
    uint8_t block[10];

    int ret = -1;

    file = fopen(filepath, "rb");
    if (file == NULL) {
        return -1;
    }

    fseek(file, 0, SEEK_END); // seek to end of file
    int file_size = ftell(file); // get current file pointer
    fseek(file, 0, SEEK_SET); // seek back to beginning of file

    if (fread(block, 1, sizeof(block), file) != sizeof(block)) {
        //read file error
        goto end;
    }

    int offset = skipID3v2Tag(block, sizeof(block));
    fseek(file, offset, SEEK_SET);

    if (fread(block, 1, 4, file) != 4) {
        //read file error
        goto end;
    }

    if (strncmp((char *)block, "fLaC", 4) != 0) {
        //wrong flac file
        goto end;
    }

    while (!feof(file)) {
        if (fread(block, 1, 4, file) != 4) {
            break;
        }
        uint8_t block_type = block[0] & 0x7f;
        uint8_t is_last_block = block[0] & 0x80;
        int block_size = bytes_to_int(&block[1], 3);

        // Metadata Streaminfo block
        if (block_type == 0) {

            uint8_t *buf = malloc(block_size);
            if (buf == NULL) {
                //error allocating memory
                break;
            }
            if (fread(buf, 1, block_size, file) != block_size) {
                //read file error
                free(buf);
                break;
            }
            /*
            https://xiph.org/flac/format.html#metadata_block_streaminfo
            16 (unsigned short)  | The minimum block size (in samples)
            used in the stream.
            16 (unsigned short)  | The maximum block size (in samples)
            used in the stream. (Minimum blocksize
                == maximum blocksize) implies a
            fixed-blocksize stream.
            24 (3 char[])        | The minimum frame size (in bytes) used
            in the stream. May be 0 to imply the
            value is not known.
            24 (3 char[])        | The maximum frame size (in bytes) used
            in the stream. May be 0 to imply the
            value is not known.
            20 (8 unsigned char) | Sample rate in Hz. Though 20 bits are
            available, the maximum sample rate is
            limited by the structure of frame
            headers to 655350Hz. Also, a value of 0
            is invalid.
            3  (^)               | (number of channels)-1. FLAC supports
            from 1 to 8 channels
            5  (^)               | (bits per sample)-1. FLAC supports from
            4 to 32 bits per sample. Currently the
            reference encoder and decoders only
            support up to 24 bits per sample.
            36 (^)               | Total samples in stream. 'Samples'
            means inter-channel sample, i.e. one
            second of 44.1Khz audio will have 44100
            samples regardless of the number of
            channels. A value of zero here means
            the number of total samples is unknown.
            128 (16 char[])      | MD5 signature of the unencoded audio
            data. This allows the decoder to
            determine if an error exists in the
            audio data even when the error does not
            result in an invalid bitstream.
            */
            int samplerate = bytes_to_int(&buf[10], 3) >> 4;
            //printf("samplerate %d\n", samplerate);

            int channels = ((buf[12] >> 1) & 0x07) + 1;
            //printf("channels %d\n", channels);
            int bit_depth = ((buf[12] & 1) << 4) + ((buf[13] & 0xF0) >> 4);
            bit_depth += 1;
            //printf("bit_depth %d\n", bit_depth);

            uint8_t sample_bytes[5];
            sample_bytes[0] = (buf[13] & 0x0F);
            memcpy(&sample_bytes[1], &buf[14], 4);
            int total_samples = bytes_to_int(sample_bytes, 5);
            f_duration = (float)total_samples / (float)samplerate;
            *duration = (int)f_duration;
            if (*duration > 0) {
                *avg_bitrate = file_size / *duration * 8;
            }
            free(buf);
            ret = 0;
            break;
        }
    }
end: fclose(file);
    return ret;
}