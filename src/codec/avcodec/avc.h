/**
    Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

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

#ifndef _AVC_H
#define _AVC_H

#include <ffmpeg/avcodec.h>
#include <tcvp_types.h>

typedef struct avc_codec {
    AVCodecContext *ctx;

    /* audio */
    char *buf;

    /* video */
    uint64_t pts;
    uint64_t ptsn, ptsd;
    AVFrame *frame;

    int have_params;
} avc_codec_t;

extern enum CodecID avc_codec_id(char *);
extern char *avc_codec_name(enum CodecID id);
extern void avc_free_packet(void *);

#endif
