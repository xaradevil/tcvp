/**
    Copyright (C) 2003-2007  Michael Ahlberg, Måns Rullgård

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

#include <stdlib.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcendian.h>
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
	    dts = data + 14;
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
/* 	tc2_print("MPEGPS", TC2_PRINT_DEBUG, "stream %x, pts %lli\n", */
/* 		  pes->stream_id, pes->pts); */
    }

    if(dts){
	pes->flags |= PES_FLAG_DTS;
	pes->dts = (htob_16(unaligned16(dts+3)) & 0xfffe) >> 1;
	pes->dts |= (htob_16(unaligned16(dts+1)) & 0xfffe) << 14;
	pes->dts |= (uint64_t) (*dts & 0xe) << 29;
/* 	fprintf(stderr, "MPEGPS: stream %x, dts %lli\n", */
/* 		pes->stream_id, pes->dts); */
    }

    pes->data = data + hl;
    if(pkl)
	pes->size = pkl + 6;

    return 0;
}

extern void
mpeg_free(muxed_stream_t *ms)
{
    if(ms->streams)
	free(ms->streams);
    if(ms->used_streams)
	free(ms->used_streams);
}

extern muxed_stream_t *
mpeg_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *t)
{
    muxed_stream_t *ms;

    ms = mpegps_open(name, u, cs, t);
    if(!ms)
	ms = mpegts_open(name, u, cs, t);
    if(!ms)
	return NULL;

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));

    return ms;
}

static mpeg_stream_type_t mpeg_stream_types[] = {
    { 0x01, 0xe0, "video/mpeg"  },
    { 0x02, 0xe0, "video/mpeg2" },
    { 0x03, 0xc0, "audio/mpeg"  },
    { 0x03, 0xc0, "audio/mp2"   },
    { 0x03, 0xc0, "audio/mp3"   },
    { 0x04, 0xc0, "audio/mpeg"  },
    { 0x0f, 0xc0, "audio/aac"   },
    { 0x10, 0xe0, "video/mpeg4" },
    { 0x11, 0xc0, "audio/aac"   },
    { 0x1b, 0xe0, "video/h264"  },
    { 0x81, 0xbd, "audio/ac3"   }
};

static tcfraction_t frame_rates[16] = {
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

static tcfraction_t aspect_ratios[16] = {
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

    for(i = 0; i < tcvp_demux_mpeg_conf_private_type_count; i++)
	if(!strcmp(codec, tcvp_demux_mpeg_conf_private_type[i].codec))
	    return (mpeg_stream_type_t *)
		tcvp_demux_mpeg_conf_private_type + i;

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

static int
dvb_descriptor(stream_t *s, uint8_t *d, unsigned int tag, unsigned int len)
{
    switch(tag){
    case DVB_AC_3_DESCRIPTOR:
        s->audio.codec = "audio/ac3";
        break;
    case DVB_DTS_DESCRIPTOR:
        s->audio.codec = "audio/dts";
        break;
    case DVB_AAC_DESCRIPTOR:
        s->audio.codec = "audio/aac";
        break;
    }

    return 0;
}

extern int
mpeg_descriptor(stream_t *s, u_char *d)
{
    int tag = d[0];
    int len = d[1];

    tc2_print("MPEG", TC2_PRINT_DEBUG, "descriptor %3d [%2x], %d bytes\n",
              tag, tag, len);

    switch(tag){
    case VIDEO_STREAM_DESCRIPTOR:
	s->video.frame_rate = frame_rates[(d[2] >> 3) & 0xf];
#if 0
	if(d[2] & 0x4)
	    fprintf(stderr, "MPEG: MPEG 1 only\n");
	if(d[2] & 0x2)
	    fprintf(stderr, "MPEG: constrained parameter\n");
	if(!(d[2] & 0x4)){
	    fprintf(stderr, "MPEG: esc %i profile %i, level %i\n",
		    d[3] >> 7, (d[3] >> 4) & 0x7, d[3] & 0xf);
	}
#endif
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
        if(len > 3)
            memcpy(s->audio.language, d + 2, 3);
	break;
    }

    if(tag >= 64){
        if(tcvp_demux_mpeg_conf_dvb)
            dvb_descriptor(s, d, tag, len);
    }

    return len + 2;
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
	if(!s->video.width || !s->video.height)
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

extern mpeg_stream_type_t *
mpeg_stream_type_id(int st)
{
    int i;

    for(i = 0; i < nstream_types; i++)
	if(mpeg_stream_types[i].mpeg_stream_type == st)
	    return mpeg_stream_types + i;

    for(i = 0; i < tcvp_demux_mpeg_conf_private_type_count; i++)
	if(tcvp_demux_mpeg_conf_private_type[i].id == st)
	    return (mpeg_stream_type_t *)
		tcvp_demux_mpeg_conf_private_type + i;

    return NULL;
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

    if(size > 0){
	if(pklen > 0xffff)
	    tc2_print("MPEG", TC2_PRINT_WARNING, "oversized PES packet: %i\n",
		      pklen);
    } else {
	pklen = 0;
    }

    st_unaligned16(htob_16(pklen), plen);

    return hdrl + 9;
}
