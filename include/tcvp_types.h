/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**/

#ifndef _TCVP_H
#define _TCVP_H

#include <stdint.h>
#include <tcalloc.h>

typedef struct packet packet_t;
struct packet {
    u_char **data;
    int *sizes;
    int planes;
    uint64_t pts;
    void (*free)(packet_t *);
    void *private;
};

#define STREAM_TYPE_VIDEO 1
#define STREAM_TYPE_AUDIO 2

#define PIXEL_FORMATS 3

#define PIXEL_FORMAT_YV12 1
#define PIXEL_FORMAT_I420 2
#define PIXEL_FORMAT_YUY2 3

#define STREAM_COMMON				\
    int stream_type;				\
    char *codec;				\
    void *codec_data;				\
    int codec_data_size;			\
    uint64_t start_time

typedef struct video_stream {
    STREAM_COMMON;
    struct {
	int num, den;
    } frame_rate;
    int width, height;
    u_long frames;
    int pixel_format;
} video_stream_t;

typedef struct audio_stream {
    STREAM_COMMON;
    int sample_rate;
    int channels;
    u_long samples;
    int bit_rate;
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

typedef struct muxed_stream muxed_stream_t;
struct muxed_stream {
    int n_streams;
    stream_t *streams;
    int *used_streams;
    packet_t *(*next_packet)(muxed_stream_t *, int stream);
    int (*close)(muxed_stream_t *);
    int (*seek)(muxed_stream_t *, uint64_t);
    void *private;
};

typedef struct tcvp_pipe tcvp_pipe_t;
struct tcvp_pipe {
    int (*input)(tcvp_pipe_t *, packet_t *);
    int (*start)(tcvp_pipe_t *);
    int (*stop)(tcvp_pipe_t *);
    int (*free)(tcvp_pipe_t *);
    int (*probe)(tcvp_pipe_t *, packet_t *, stream_t *);
    int (*flush)(tcvp_pipe_t *, int drop);
    int (*buffer)(tcvp_pipe_t *, float);
    tcvp_pipe_t *next;
    void *private;
};

#define PROBE_OK    1
#define PROBE_FAIL  2
#define PROBE_AGAIN 3

typedef struct tcvp_key_event {
    int type;
    char *key;
} tcvp_key_event_t;

typedef struct tcvp_open_event {
    int type;
    char *file;
} tcvp_open_event_t;

typedef struct {
    int type;
} tcvp_start_event_t, tcvp_stop_event_t, tcvp_close_event_t;

typedef struct tcvp_seek_event {
    int type;
    int64_t time;
    int how;
} tcvp_seek_event_t;

typedef struct tcvp_timer_event {
    int type;
    uint64_t time;
} tcvp_timer_event_t;

typedef struct tcvp_state_event {
    int type;
    int state;
} tcvp_state_event_t;

typedef union tcvp_event {
    int type;
    tcvp_key_event_t key;
    tcvp_open_event_t open;
    tcvp_start_event_t start;
    tcvp_stop_event_t stop;
    tcvp_close_event_t close;
    tcvp_seek_event_t seek;
    tcvp_timer_event_t timer;
    tcvp_state_event_t state;
} tcvp_event_t;

#define TCVP_KEY       1
#define TCVP_OPEN      2
#define TCVP_START     3
#define TCVP_STOP      4
#define TCVP_PAUSE     5

#define TCVP_SEEK      6
#define TCVP_SEEK_ABS  0
#define TCVP_SEEK_REL  1

#define TCVP_CLOSE     7
#define TCVP_TIMER     8

#define TCVP_STATE     9
#define TCVP_STATE_PLAYING 0
#define TCVP_STATE_END     1
#define TCVP_STATE_ERROR   2
#define TCVP_STATE_STOPPED 3

#define tcvp_alloc_event() tcalloc(sizeof(tcvp_event_t))

#endif
