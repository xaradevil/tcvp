/**
    Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

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

static int dts_channels[64] = {
    1, 2, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6, 6, 7, 8, 8
};

static int dts_samplerates[16] = {
    0, 8000, 16000, 32000, 0, 0,
    11025, 22050, 44100, 0, 0,
    12000, 24000, 48000
};

static int dts_bitrates[32] = {
    32000,
    56000,
    64000,
    96000,
    112000,
    128000,
    192000,
    224000,
    256000,
    320000,
    384000,
    448000,
    512000,
    576000,
    640000,
    768000,
    960000,
    1024000,
    1152000,
    1280000,
    1344000,
    1408000,
    1411200,
    1472000,
    1536000,
    1920000,
    2048000,
    3072000,
    3840000
};

static int
dts16_header(u_char *head, mp3_frame_t *mf)
{
    tcvp_bits_t bits;
    u_int v;

    tcvp_bits_init(&bits, head, 11);

    v = tcvp_bits_get(&bits, 32);
    if(v != 0x7ffe8001)
        return -1;

    tcvp_bits_get(&bits, 1);    /* frame type */
    tcvp_bits_get(&bits, 5);    /* deficit sample count */
    tcvp_bits_get(&bits, 1);    /* CRC present */
    mf->samples = 32 * tcvp_bits_get(&bits, 7);
    v = tcvp_bits_get(&bits, 14);
    if(v < 95)
        return -1;
    mf->size = v + 1;
    v = tcvp_bits_get(&bits, 6);
    mf->channels = dts_channels[v];
    v = tcvp_bits_get(&bits, 4);
    if(!dts_samplerates[v])
        return -1;
    mf->sample_rate = dts_samplerates[v];
    v = tcvp_bits_get(&bits, 5);
    mf->bitrate = dts_bitrates[v];
    tcvp_bits_get(&bits, 1);    /* embedded downmix */
    tcvp_bits_get(&bits, 1);    /* embedded dynamic range */
    tcvp_bits_get(&bits, 1);    /* embedded timestamp */
    tcvp_bits_get(&bits, 1);    /* auxiliary data */
    tcvp_bits_get(&bits, 1);    /* hdcd */
    tcvp_bits_get(&bits, 3);    /* extension audio descriptor */
    tcvp_bits_get(&bits, 1);    /* extended coding */
    tcvp_bits_get(&bits, 1);    /* audio sync word insertion */
    v = tcvp_bits_get(&bits, 2); /* lfe */
    if(v == 3)
        return -1;
    if(v)
        mf->channels++;

    mf->layer = 5;

    return 0;
}

static void
dts_pack(u_char *dst, u_char *src)
{
    tcvp_bits_t d;
    int i;

    tcvp_bits_init(&d, dst, 12);

    for(i = 0; i < 7; i++){
        u_int b = unaligned16(src);
        tcvp_bits_put(&d, b, 14);
        src += 2;
    }

    tcvp_bits_flush(&d);
}

static void
dts_swap(u_char *dst, u_char *src, int n)
{
    while(n > 0){
        st_unaligned16(bswap_16(unaligned16(src)), dst);
        src += 2;
        dst += 2;
        n -= 2;
    }
}

static int
dts16s_header(u_char *head, mp3_frame_t *mf)
{
    u_char buf[12];
    dts_swap(buf, head, 12);
    return dts16_header(buf, mf);
}

static int
dts14_header(u_char *head, mp3_frame_t *mf)
{
    u_char buf[12];

    dts_pack(buf, head);
    if(dts16_header(buf, mf))
        return -1;
    mf->size = 8 * mf->size / 7;
    return 0;
}

static int
dts14s_header(u_char *head, mp3_frame_t *mf)
{
    u_char buf1[14], buf2[12];
    dts_swap(buf1, head, 14);
    dts_pack(buf2, buf1);
    if(dts16_header(buf2, mf))
        return -1;
    mf->size = 8 * mf->size / 7;
    return 0;
}

mp3_header_parser_t dts16_parser  = { dts16_header, 12, "DTS" };
mp3_header_parser_t dts16s_parser = { dts16s_header, 12, "DTS" };
mp3_header_parser_t dts14_parser  = { dts14_header, 14, "DTS" };
mp3_header_parser_t dts14s_parser = { dts14s_header, 14, "DTS" };
