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
    u_char *pts = NULL, *dts = NULL;
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
	if(data[7] & 0x40){
	    dts = data + 13;
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

    pes->flags = 0;

    if(pts){
	pes->flags |= PES_FLAG_PTS;
	pes->pts = (htob_16(unaligned16(pts+3)) & 0xfffe) >> 1;
	pes->pts |= (htob_16(unaligned16(pts+1)) & 0xfffe) << 14;
	pes->pts |= (uint64_t) (*pts & 0xe) << 29;
/* 	fprintf(stderr, "MPEGPS: stream %x, pts %lli\n", */
/* 		pes->stream_id, pes->pts); */
    }

    if(dts){
	pes->flags |= PES_FLAG_DTS;
	pes->dts = (htob_16(unaligned16(dts+3)) & 0xfffe) >> 1;
	pes->dts |= (htob_16(unaligned16(dts+1)) & 0xfffe) << 14;
	pes->dts |= (uint64_t) (*dts & 0xe) << 29;
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
mpeg_open(char *name, conf_section *cs, tcvp_timer_t **t)
{
    muxed_stream_t *ms;

    ms = mpegps_open(name, cs, t);
    if(!ms)
	ms = mpegts_open(name, cs, t);
    if(!ms)
	return NULL;

    ms->file = strdup(name);
    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));

    return ms;
}

mpeg_stream_type_t mpeg_stream_types[] = {
    { 0x01, 0xe0, STREAM_TYPE_VIDEO, "video/mpeg"  },
    { 0x02, 0xe0, STREAM_TYPE_VIDEO, "video/mpeg2" },
    { 0x03, 0xc0, STREAM_TYPE_AUDIO, "audio/mpeg"  },
    { 0x03, 0xc0, STREAM_TYPE_AUDIO, "audio/mp2"   },
    { 0x03, 0xc0, STREAM_TYPE_AUDIO, "audio/mp3"   },
    { 0x04, 0xc0, STREAM_TYPE_AUDIO, "audio/mpeg"  },
    { 0x10, 0xe0, STREAM_TYPE_VIDEO, "video/mpeg4" },
    { 0x1a, 0xe0, STREAM_TYPE_VIDEO, "video/h264"  },
    { 0x81, 0xbd, STREAM_TYPE_AUDIO, "audio/ac3"   }
};

tcfraction_t frame_rates[16] = {
    { 0,     0    },
    { 24000, 1001 },
    { 24,    1    },
    { 25,    1    },
    { 30000, 1001 },
    { 30,    1    },
    { 50,    1    },
    { 60000, 1001 },
    { 60,    1    }
};

tcfraction_t aspect_ratios[16] = {
    [2] = { 4,   3   },
    [3] = { 16,  9   },
    [4] = { 221, 100 }
};

static int nstream_types =
    sizeof(mpeg_stream_types) / sizeof(mpeg_stream_types[0]);

extern mpeg_stream_type_t *
mpeg_stream_type(char *codec)
{
    int i;

    for(i = 0; i < nstream_types; i++)
	if(!strcmp(codec, mpeg_stream_types[i].codec))
	    return &mpeg_stream_types[i];

    return NULL;
}

static int
frame_rate_index(tcfraction_t *f)
{
    int i;

    for(i = 0; i < 16; i++)
	if(f->num == frame_rates[i].num &&
	   f->den == frame_rates[i].den)
	    return i;

    return 0;
}

static int
aspect_ratio_index(tcfraction_t *f)
{
    int i;

    for(i = 0; i < 16; i++)
	if(f->num == aspect_ratios[i].num &&
	   f->den == aspect_ratios[i].den)
	    return i;

    return 0;
}

extern int
mpeg_descriptor(stream_t *s, u_char *d)
{
    int tag = d[0];
    int len = d[1];

    switch(tag){
    case VIDEO_STREAM_DESCRIPTOR:
	s->video.frame_rate = frame_rates[(d[2] >> 3) & 0xf];
	if(d[2] & 0x4)
	    fprintf(stderr, "MPEG: MPEG 1 only\n");
	if(d[2] & 0x2)
	    fprintf(stderr, "MPEG: constrained parameter\n");
	if(!(d[2] & 0x4)){
	    fprintf(stderr, "MPEG: esc %i profile %i, level %i\n",
		    d[3] >> 7, (d[3] >> 4) & 0x7, d[3] & 0xf);
	}
	break;

    case AUDIO_STREAM_DESCRIPTOR:
	break;

    case TARGET_BACKGROUND_GRID_DESCRIPTOR: {
	int n = htob_32(unaligned32(d + 2));
	s->video.width = (n >> 18) & 0x3fff;
	s->video.height = (n >> 4) & 0x3fff;

	n &= 0xf;
	if(n == 1){
	    s->video.aspect.num = s->video.width;
	    s->video.aspect.den = s->video.height;
	    tcreduce(&s->video.aspect);
	} else if(aspect_ratios[n].num){
	    s->video.aspect = aspect_ratios[n];
	}
	break;
    }
    case ISO_639_LANGUAGE_DESCRIPTOR:
	break;
    }

    return len;
}

extern int
write_mpeg_descriptor(stream_t *s, int tag, u_char *d, int size)
{
    u_char *p = d;
    int i;

    switch(tag){
    case VIDEO_STREAM_DESCRIPTOR:
	if(size < 5)
	    return 0;
	i = frame_rate_index(&s->video.frame_rate);
	if(!i)
	    return 0;
	*p++ = tag;
	*p++ = 3;
	*p++ = i << 3;
	*p++ = 0x48;
	*p++ = 0x5f;
	return 5;

    case TARGET_BACKGROUND_GRID_DESCRIPTOR:
	if(size < 6)
	    return 0;
	if(!s->video.aspect.num){
	    i = 1;
	} else {
	    i = aspect_ratio_index(&s->video.aspect);
	    if(!i){
		tcfraction_t f = { s->video.width, s->video.height };
		tcreduce(&f);
		if(f.num == s->video.aspect.num &&
		   f.den == s->video.aspect.den)
		    i = 1;
	    }
	}
	if(!i)
	    return 0;
	*p++ = tag;
	*p++ = 4;
	st_unaligned32(htob_32((s->video.width << 18) |
			       (s->video.height << 4) | i),
		       p);
	return 6;
    }

    return 0;
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
	uint64_t pts = 0, dts = 0;

	pklen += 3;
	*p++ = 0x80;

	if(flags & PES_FLAG_PTS){
	    pts = va_arg(args, uint64_t);
	    pflags |= 0x80;
	    hdrl += 5;
	}

	if(flags & PES_FLAG_DTS){
	    dts = va_arg(args, uint64_t);
	    pflags |= 0x40;
	    hdrl += 5;
	}

	*p++ = pflags;
	*p++ = hdrl;

	if(flags & PES_FLAG_PTS){
	    *p++ = ((pts >> 29) & 0xe) | (flags & PES_FLAG_DTS? 0x31: 0x21);
	    st_unaligned16(htob_16(((pts >> 14) & 0xfffe) | 1), p);
	    p += 2;
	    st_unaligned16(htob_16(((pts << 1) & 0xfffe) | 1), p);
	    p += 2;
	}

	if(flags & PES_FLAG_DTS){
	    *p++ = ((dts >> 29) & 0xe) | 0x11;
	    st_unaligned16(htob_16(((dts >> 14) & 0xfffe) | 1), p);
	    p += 2;
	    st_unaligned16(htob_16(((dts << 1) & 0xfffe) | 1), p);
	    p += 2;
	}

	pklen += hdrl;
    }

    st_unaligned16(htob_16(pklen), plen);

    return hdrl + 9;
}
