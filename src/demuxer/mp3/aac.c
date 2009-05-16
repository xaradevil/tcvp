/**
    Copyright (C) 2003-2005  Michael Ahlberg, Måns Rullgård

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
**/

#include <stdlib.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <tcvp_bits.h>
#include <mp3_tc2.h>
#include "mp3.h"

static int aac_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

static int aac_channels[8] = {
    0, 1, 2, 3, 4, 5, 6, 8
};

static int
aac_header(u_char *head, mp3_frame_t *mf)
{
    tcvp_bits_t bits;
    u_int id, layer, profile, sr, ch, rdb;

    tcvp_bits_init(&bits, head, 7);

    if(tcvp_bits_get(&bits, 12) != 0xfff)
        return -1;

    id = tcvp_bits_get(&bits, 1);
    layer = tcvp_bits_get(&bits, 2);
    tcvp_bits_get(&bits, 1);    /* protection_absent */
    profile = tcvp_bits_get(&bits, 2);
    sr = tcvp_bits_get(&bits, 4);
    if(!aac_sample_rates[sr])
        return -1;
    tcvp_bits_get(&bits, 1);    /* private_bit */
    ch = tcvp_bits_get(&bits, 3);
    if(!aac_channels[ch])
        return -1;
    tcvp_bits_get(&bits, 1);    /* original/copy */
    tcvp_bits_get(&bits, 1);    /* home */

    /* adts_variable_header */
    tcvp_bits_get(&bits, 1);    /* copyright_identification_bit */
    tcvp_bits_get(&bits, 1);    /* copyright_identification_start */
    mf->size = tcvp_bits_get(&bits, 13);
    tcvp_bits_get(&bits, 11);   /* adts_buffer_fullness */
    rdb = tcvp_bits_get(&bits, 2);

    mf->channels = aac_channels[ch];
    mf->sample_rate = aac_sample_rates[sr];
    mf->samples = (rdb + 1) * 1024;
    mf->bitrate = mf->size * 8 * mf->sample_rate / mf->samples;
    mf->layer = 3;

    return 0;
}

mp3_header_parser_t aac_parser = { aac_header, 8, "AAC" };
