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

#ifndef _MPEG_H
#define _MPEG_H

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcbyteswap.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>

typedef struct mpegpes_packet {
    int stream_id;
    int flags;
    uint64_t pts, dts;
    int size;
    u_char *data;
    u_char *hdr;
} mpegpes_packet_t;

#define PES_FLAG_PTS 0x1
#define PES_FLAG_DTS 0x2

typedef struct mpeg_stream_type {
    int mpeg_stream_type;
    int stream_id_base;
    int stream_type;
    char *codec;
} mpeg_stream_type_t;

extern mpeg_stream_type_t mpeg_stream_types[];
extern tcfraction_t frame_rates[16];

#define MPEGTS_SYNC 0x47

#define PACK_HEADER              0xba
#define SYSTEM_HEADER            0xbb

#define PROGRAM_STREAM_MAP       0xbc
#define PRIVATE_STREAM_1         0xbd
#define PADDING_STREAM           0xbe
#define PRIVATE_STREAM_2         0xbf
#define ECM_STREAM               0xf0
#define EMM_STREAM               0xf1
#define DSMCC_STREAM             0xf2
#define ISO_13522_STREAM         0xf3
#define H222_A_STREAM            0xf4
#define H222_B_STREAM            0xf5
#define H222_C_STREAM            0xf6
#define H222_D_STREAM            0xf7
#define H222_E_STREAM            0xf8
#define ANCILLARY_STREAM         0xf9
#define ISO_14496_SL_STREAM      0xfa
#define ISO_14496_FLEXMUX_STREAM 0xfb
#define PROGRAM_STREAM_DIRECTORY 0xff

#define VIDEO_STREAM_DESCRIPTOR                  2
#define AUDIO_STREAM_DESCRIPTOR                  3
#define HIERARCHY_DESCRIPTOR                     4
#define REGISTRATION_DESCRIPTOR                  5
#define DATA_STREAM_ALIGNMENT_DESCRIPTOR         6
#define TARGET_BACKGROUND_GRID_DESCRIPTOR        7
#define VIDEO_WINDOW_DESCRIPTOR                  8
#define CA_DESCRIPTOR                            9
#define ISO_639_LANGUAGE_DESCRIPTOR             10
#define SYSTEM_CLOCK_DESCRIPTOR                 11
#define MULTIPLEX_BUFFER_UTILIZATION_DESCRIPTOR 12
#define COPYRIGHT_DESCRIPTOR                    13
#define MAXIMUM_BITRATE_DESCRIPTOR              14
#define PRIVATE_DATA_INDICATOR_DESCRIPTOR       15
#define SMOOTHING_BUFFER_DESCRIPTOR             16
#define STD_DESCRIPTOR                          17
#define IBP_DESCRIPTOR                          18

#define min(a,b) ((a)<(b)?(a):(b))

#define getuint(s)				\
static inline uint##s##_t			\
getu##s(url_t *f)				\
{						\
    uint##s##_t v;				\
    f->read(&v, sizeof(v), 1, f);		\
    v = htob_##s(v);				\
    return v;					\
}

getuint(16)
getuint(32)
getuint(64)

extern int mpegpes_header(mpegpes_packet_t *pes, u_char *data, int h);
extern int stream_type2codec(int st);
extern mpeg_stream_type_t *mpeg_stream_type(char *codec);
extern int mpeg_descriptor(stream_t *s, u_char *d);
extern int write_mpeg_descriptor(stream_t *s, int tag, u_char *d, int size);
extern int write_pes_header(u_char *p, int stream_id, int size,
			    int flags, ...);
extern uint32_t mpeg_crc32(const u_char *data, int len);
extern void mpeg_free(muxed_stream_t *);

extern packet_t *mpegts_packet(muxed_stream_t *ms, int str);
extern muxed_stream_t *mpegts_open(char *, tcconf_section_t *,
				   tcvp_timer_t *);

extern packet_t *mpegps_packet(muxed_stream_t *ms, int str);
extern muxed_stream_t *mpegps_open(char *, tcconf_section_t *,
				   tcvp_timer_t *);

#endif
