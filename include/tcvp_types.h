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
#include <sys/types.h>

typedef struct packet packet_t;
struct packet {
    u_char **data;
    size_t *sizes;
    int planes;
    uint64_t pts;
    void (*free)(packet_t *);
    void *private;
};

#define STREAM_TYPE_VIDEO 1
#define STREAM_TYPE_AUDIO 2

typedef struct video_stream {
    int stream_type;
    char *codec;
    double frame_rate;
    int width, height;
    u_long frames;
} video_stream_t;

typedef struct audio_stream {
    int stream_type;
    char *codec;
    int sample_rate;
    int channels;
    u_long samples;
} audio_stream_t;

typedef union stream {
    int stream_type;
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
    tcvp_pipe_t *next;
    void *private;
};

#define PROBE_OK    1
#define PROBE_FAIL  2
#define PROBE_AGAIN 3

#endif
