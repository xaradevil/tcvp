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

#ifndef _TCVP_TYPES_H
#define _TCVP_TYPES_H

#include <stdint.h>
#include <tctypes.h>
#include <tcmath.h>

/* packet_t MUST be allocated with tcalloc */
typedef struct packet {
    int type;
    int stream;
    u_char **data;
    int *sizes;
    int planes;
    int x, y, w, h;		/* slice position */
    int flags;
    uint64_t pts, dts;
    void *private;
} packet_t;

#define TCVP_PKT_TYPE_DATA  0
#define TCVP_PKT_TYPE_FLUSH 1
#define TCVP_PKT_TYPE_STILL 2

#define TCVP_PKT_FLAG_PTS        0x1
#define TCVP_PKT_FLAG_DTS        0x2
#define TCVP_PKT_FLAG_KEY        0x4
#define TCVP_PKT_FLAG_DISCONT    0x8

#define STREAM_TYPE_VIDEO     1
#define STREAM_TYPE_AUDIO     2
#define STREAM_TYPE_MULTIPLEX 3

#define TCVP_STREAM_FLAG_INTERLACED 0x1

#define STREAM_COMMON				\
    int stream_type;				\
    char *codec;				\
    void *codec_data;				\
    int codec_data_size;			\
    uint64_t start_time;			\
    int index;					\
    int flags;					\
    int bit_rate

typedef struct video_stream {
    STREAM_COMMON;
    tcfraction_t frame_rate;
    int width, height;
    tcfraction_t aspect;
    u_long frames;
} video_stream_t;

typedef struct audio_stream {
    STREAM_COMMON;
    int sample_rate;
    int channels;
    u_long samples;
    int block_align;
    int sample_size;
} audio_stream_t;

typedef union stream {
    int stream_type;
    struct {
	STREAM_COMMON;
    } common;
    video_stream_t video;
    audio_stream_t audio;
} stream_t;

/* muxed_stream_t MUST be allocated with tcalloc */
typedef struct muxed_stream muxed_stream_t;
struct muxed_stream {
    int n_streams;
    stream_t *streams;
    int *used_streams;
    uint64_t time;
    packet_t *(*next_packet)(muxed_stream_t *, int stream);
    uint64_t (*seek)(muxed_stream_t *, uint64_t);
    void *private;
};

/* tcvp_pipe_t MUST be allocated with tcalloc */
typedef struct tcvp_pipe tcvp_pipe_t;
struct tcvp_pipe {
    stream_t format;
    int (*input)(tcvp_pipe_t *, packet_t *);
    int (*start)(tcvp_pipe_t *);
    int (*stop)(tcvp_pipe_t *);
    int (*probe)(tcvp_pipe_t *, packet_t *, stream_t *);
    int (*flush)(tcvp_pipe_t *, int drop);
    int (*buffer)(tcvp_pipe_t *, float);
    tcvp_pipe_t *next;
    void *private;
};

#define PROBE_OK      1
#define PROBE_FAIL    2
#define PROBE_AGAIN   3
#define PROBE_DISCARD 4

#endif
