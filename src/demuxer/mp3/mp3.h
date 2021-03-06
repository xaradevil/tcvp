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

#ifndef MP3_H
#define MP3_H

#include <tctypes.h>

typedef struct mp3_frame {
    int version;
    int layer;
    int bitrate;
    int sample_rate;
    int size;
    int samples;
    int channels;
} mp3_frame_t;

typedef struct mp3_header_parser {
    int (*parser)(u_char *, mp3_frame_t *);
    int header_size;
    char *tag;
} mp3_header_parser_t;

#define min(a, b) ((a)<(b)?(a):(b))

extern mp3_header_parser_t mpeg1_parser;
extern mp3_header_parser_t aac_parser;
extern mp3_header_parser_t ac3_parser;
extern mp3_header_parser_t dts16_parser;
extern mp3_header_parser_t dts16s_parser;
extern mp3_header_parser_t dts14_parser;
extern mp3_header_parser_t dts14s_parser;

#endif
