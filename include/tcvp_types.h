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

#ifndef _TCVP_TYPES_H
#define _TCVP_TYPES_H

#include <stdint.h>
#include <tctypes.h>

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
    struct {
	int num, den;
    } aspect;
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
    uint64_t time;
    char *file, *title, *performer;
    packet_t *(*next_packet)(muxed_stream_t *, int stream);
    uint64_t (*seek)(muxed_stream_t *, uint64_t);
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

#endif
