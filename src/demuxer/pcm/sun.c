/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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
#include <string.h>
#include <tctypes.h>
#include <tcbyteswap.h>
#include <pcmfmt_tc2.h>
#include <pcmmod.h>

static struct {
    char *codec;
    int bits;
} formats[] = {
    [1] =  { "audio/pcm-ulaw",      8 },
    [2] =  { "audio/pcm-u8",        8 },
    [3] =  { "audio/pcm-s16be",     16 },
    [4] =  { "audio/pcm-s24be",     24 },
    [5] =  { "audio/pcm-s32be",     32 },
    [6] =  { "audio/pcm-float",     sizeof(float) },
    [7] =  { "audio/pcm-double",    sizeof(double) },
    [23] = { "audio/adpcm-g721",    8 },
    [24] = { "audio/adpcm-g722",    8 },
    [25] = { "audio/adpcm-g723-3",  8 },
    [26] = { "audio/adpcm-g723-5",  8 },
    [27] = { "audio/adpcm-alaw",    8 }
};

#define FORMAT_MAX 27

extern muxed_stream_t *
au_open(char *name, url_t *u, tcconf_section_t *conf, tcvp_timer_t *tm)
{
    char magic[4];
    uint32_t data, size, fmt, rate, channels;
    char *codec;
    int align, brate;

    if(u->read(magic, 1, 4, u) != 4)
	return NULL;
    if(memcmp(magic, ".snd", 4))
	return NULL;

    url_getu32b(u, &data);
    url_getu32b(u, &size);
    url_getu32b(u, &fmt);
    url_getu32b(u, &rate);
    url_getu32b(u, &channels);

    if(fmt > FORMAT_MAX)
	return NULL;
    if(!formats[fmt].codec)
	return NULL;

    u->seek(u, data, SEEK_SET);

    codec = formats[fmt].codec;
    align = channels * formats[fmt].bits / 8;
    brate = rate * formats[fmt].bits * channels;

    return pcm_open(u, codec, channels, rate, size / align, brate,
		    formats[fmt].bits, NULL, 0);
}
