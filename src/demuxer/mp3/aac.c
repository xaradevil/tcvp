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

static int aac_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000
};

extern int
aac_header(u_char *head, mp3_frame_t *mf)
{
    int profile;

    if(head[0] != 0xff)
	return -1;

    if((head[1] & 0xf6) != 0xf0)
	return -1;

    if(!mf)
	return 0;

    profile = head[2] >> 6;
    mf->sample_rate = aac_sample_rates[(head[2] >> 2) & 0xf];
    mf->size = ((int) (head[3] & 0x3) << 11) +
	((int) head[4] << 3) +
	((int) (head[5] & 0xe0) >> 5);
    mf->samples = 1024;
    mf->bitrate = mf->size * 8 * mf->sample_rate / mf->samples;
    mf->layer = 3;

    return 0;
}
