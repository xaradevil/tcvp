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
#include <mp3_tc2.h>
#include "mp3.h"

static int bitrates[16][5] = {
    {  0,   0,   0,   0,   0},
    { 32,  32,  32,  32,   8},
    { 64,  48,  40,  48,  16},
    { 96,  56,  48,  56,  24},
    {128,  64,  56,  64,  32},
    {160,  80,  64,  80,  40},
    {192,  96,  80,  96,  48},
    {224, 112,  96, 112,  56},
    {256, 128, 112, 128,  64},
    {288, 160, 128, 144,  80},
    {320, 192, 160, 160,  96},
    {352, 224, 192, 176, 112},
    {384, 256, 224, 192, 128},
    {416, 320, 256, 224, 144},
    {448, 384, 320, 256, 160},
    {  0,   0,   0,   0,   0}
};

static int mpeg_sample_rates[3][4] = {
    {11025, 0, 22050, 44100},
    {12000, 0, 24000, 48000},
    { 8000, 0, 16000, 32000}
};

extern int
mp3_header(u_char *head, mp3_frame_t *mf)
{
    int c = head[1], d = head[2];
    int bx, br, sr, pad, lsf = 0;

    if(head[0] != 0xff)
	return -1;

    if((c & 0xe0) != 0xe0 ||
       ((c & 0x18) == 0x08 ||
	(c & 0x06) == 0)){
	return -1;
    }
    if((d & 0xf0) == 0xf0 ||
       (d & 0x0c) == 0x0c){
	return -1;
    }

    if(!mf)
	return 0;

    mf->version = (c >> 3) & 0x3;
    mf->layer = 3 - ((c >> 1) & 0x3);
    bx = mf->version == 3? mf->layer: 3 + (mf->layer > 1);
    br = (d >> 4) & 0xf;
    if(!bitrates[br][bx])
	return -1;

    sr = (d >> 2) & 3;
    pad = (d >> 1) & 1;
    mf->bitrate = bitrates[br][bx] * 1000;
    mf->sample_rate = mpeg_sample_rates[sr][mf->version];
    switch(mf->layer){
    case 2:
	lsf = ~mf->version & 1;
    case 1:
	mf->size = 144 * mf->bitrate / (mf->sample_rate << lsf) + pad;
	mf->samples = 1152;
	break;
    case 0:
	mf->size = (12 * mf->bitrate / mf->sample_rate + pad) * 4;
	mf->samples = 384;
	break;
    }

#ifdef DEBUG
    tc2_print("MP3", TC2_PRINT_DEBUG,
	      "layer %i, version %i, rate %i, size %i\n",
	      mf->layer, mf->version, mf->bitrate, mf->size);
#endif

    return 0;
}
