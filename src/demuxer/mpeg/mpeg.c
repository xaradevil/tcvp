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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcbyteswap.h>
#include <stdarg.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

extern int
mpegpes_header(mpegpes_packet_t *pes, u_char *data, int h)
{
    int hl, pkl = 0;
    u_char *pts = NULL;
    u_char c;

    data -= h;

    if(h == 0)
	if((htob_32(unaligned32(data)) >> 8) != 1)
	    return -1;

    if(h < 4)
	pes->stream_id = data[3];
    if(h < 5)
	pkl = htob_16(unaligned16(data+4));

    c = data[6];
    if((c & 0xc0) == 0x80){
	hl = data[8] + 9;
	if(data[7] & 0x80){
	    pts = data + 9;
	}
    } else {
	hl = 6;
	while(c == 0xff)
	    c = data[++hl];
	if((c & 0xc0) == 0x40){
	    hl += 2;
	    c = data[hl];
	}
	if((c & 0xe0) == 0x20){
	    pts = data + hl;
	    hl += 4;
	}
	if((c & 0xf0) == 0x30){
	    hl += 5;
	}
	hl++;
    }

    if(pts){
	pes->pts_flag = 1;
	pes->pts = (htob_16(unaligned16(pts+3)) & 0xfffe) >> 1;
	pes->pts |= (htob_16(unaligned16(pts+1)) & 0xfffe) << 14;
	pes->pts |= (uint64_t) (*pts & 0xe) << 29;
/* 	fprintf(stderr, "MPEGPS: stream %x, pts %lli\n", */
/* 		pes->stream_id, pes->pts); */
    } else {
	pes->pts_flag = 0;
    }
    pes->data = data + hl;
    if(pkl)
	pes->size = pkl + 6;

    return 0;
}

extern void
mpeg_free(muxed_stream_t *ms)
{
    if(ms->file)
	free(ms->file);
    if(ms->title)
	free(ms->title);
    if(ms->performer)
	free(ms->performer);
    if(ms->streams)
	free(ms->streams);
    if(ms->used_streams)
	free(ms->used_streams);
}

extern muxed_stream_t *
mpeg_open(char *name, conf_section *cs)
{
    muxed_stream_t *ms;

    ms = mpegps_open(name);
    if(!ms)
	ms = mpegts_open(name);
    if(!ms)
	return NULL;

    ms->file = strdup(name);
    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));

    return ms;
}

struct mpeg_stream_type mpeg_stream_types[] = {
    { 0x01, STREAM_TYPE_VIDEO, "video/mpeg"  },
    { 0x02, STREAM_TYPE_VIDEO, "video/mpeg"  },
    { 0x03, STREAM_TYPE_AUDIO, "audio/mpeg"  },
    { 0x03, STREAM_TYPE_AUDIO, "audio/mp2"   },
    { 0x03, STREAM_TYPE_AUDIO, "audio/mp3"   },
    { 0x04, STREAM_TYPE_AUDIO, "audio/mpeg"  },
    { 0x10, STREAM_TYPE_VIDEO, "video/mpeg4" },
    { 0x1a, STREAM_TYPE_VIDEO, "video/h264"  }
};

static int nstream_types =
    sizeof(mpeg_stream_types) / sizeof(mpeg_stream_types[0]);

extern int
codec2stream_type(char *codec)
{
    int i;

    for(i = 0; i < nstream_types; i++)
	if(!strcmp(codec, mpeg_stream_types[i].codec))
	    return mpeg_stream_types[i].mpeg_stream_type;

    return -1;
}

extern int
stream_type2codec(int st)
{
    int i;

    for(i = 0; i < nstream_types; i++)
	if(mpeg_stream_types[i].mpeg_stream_type == st)
	    return i;

    return -1;
}

extern int
write_pes_header(u_char *p, int stream_id, int size, int flags, ...)
{
    va_list args;
    u_char *plen;
    int hdrl = 0, pklen = size;

    va_start(args, flags);

    st_unaligned32(htob_32(stream_id | 0x100), p);
    p += 4;
    plen = p;
    p += 2;

    if(stream_id != PADDING_STREAM &&
       stream_id != PRIVATE_STREAM_2 &&
       stream_id != ECM_STREAM &&
       stream_id != EMM_STREAM &&
       stream_id != PROGRAM_STREAM_DIRECTORY &&
       stream_id != DSMCC_STREAM &&
       stream_id != H222_E_STREAM &&
       stream_id != SYSTEM_HEADER){
	int pflags = 0;
	uint64_t pts;

	pklen += 3;
	*p++ = 0x80;

	if(flags & PES_FLAG_PTS){
	    pts = va_arg(args, uint64_t);
	    pflags |= 0x80;
	    hdrl += 5;
	}

	*p++ = pflags;
	*p++ = hdrl;

	if(flags & PES_FLAG_PTS){
	    *p++ = ((pts >> 29) & 0xe) | 0x21;
	    st_unaligned16(htob_16(((pts >> 14) & 0xfffe) | 1), p);
	    p += 2;
	    st_unaligned16(htob_16(((pts << 1) & 0xfffe) | 1), p);
	    p += 2;
	}
	pklen += hdrl;
    }

    st_unaligned16(htob_16(pklen), plen);

    return hdrl + 9;
}
