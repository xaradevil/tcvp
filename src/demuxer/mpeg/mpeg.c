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
	pes->size = pkl - hl;

    return 0;
}

static void
mpeg_free(void *p)
{
    muxed_stream_t *ms = p;
    mpeg_stream_t *s = ms->private;

    if(ms->file)
	free(ms->file);
    if(ms->title)
	free(ms->title);
    if(ms->performer)
	free(ms->performer);

    s->stream->close(s->stream);
    free(s->imap);
    free(s->pts);
    free(s);
}

extern muxed_stream_t *
mpeg_open(char *name, conf_section *cs)
{
    muxed_stream_t *ms;
    mpeg_stream_t *mf;
    url_t *f;

    if(!(f = url_open(name, "r")))
	return NULL;

    ms = tcallocdz(sizeof(*ms), NULL, mpeg_free);
    ms->file = strdup(name);

    mf = calloc(1, sizeof(*mf));
    mf->stream = f;

    ms->private = mf;

    if(!mpegps_getinfo(ms)){
	ms->next_packet = mpegps_packet;
    } else if(!mpegts_getinfo(ms)){
	ms->next_packet = mpegts_packet;
    } else {
	tcfree(ms);
	return NULL;
    }

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    mf->pts = calloc(ms->n_streams, sizeof(*mf->pts));

    return ms;
}

struct mpeg_stream_type mpeg_stream_types[256] = {
    [0x1 ... 0x2] = { STREAM_TYPE_VIDEO, "video/mpeg" },
    [0x3 ... 0x4] = { STREAM_TYPE_AUDIO, "audio/mpeg" },
    [0x10] = { STREAM_TYPE_VIDEO, "video/mpeg4" },
    [0x1a] = { STREAM_TYPE_VIDEO, "video/h264" }
};
