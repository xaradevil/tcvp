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

#define _ISOC99_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcbyteswap.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

typedef struct mpegps_stream {
    url_t *stream;
    int *imap;
    int rate;
} mpegps_stream_t;

static mpegpes_packet_t *
mpegpes_packet(mpegps_stream_t *s, int pedantic)
{
    mpegpes_packet_t *pes = NULL;

    do {
	uint32_t stream_id;
	int scode = 0, pklen, zc = 0, i = pedantic? 3: 0x10000;

	while(i--){
	    scode = url_getc(s->stream);
	    if(scode == 0){
		zc++;
	    } else if(zc == 2 && scode == 1){
		break;
	    } else if(scode < 0){
		return NULL;
	    } else {
		zc = 0;
	    }
	}

	if(zc != 2 || scode != 1){
	    return NULL;
	}

	stream_id = url_getc(s->stream);

	if(stream_id == PACK_HEADER){
	    int b = url_getc(s->stream) & 0xc0;
	    char foo[8];
	    if(b == 0x40){
		int sl;
		s->stream->read(foo, 1, 8, s->stream);
		sl = url_getc(s->stream) & 7;
		while(sl--)
		    url_getc(s->stream);
	    } else {
		s->stream->read(foo, 1, 7, s->stream);
	    }
	    continue;
	} else if(stream_id == 0xb9){
	    return NULL;
	}

	pklen = getu16(s->stream);

	if(stream_id == PROGRAM_STREAM_MAP){
	    pes = malloc(sizeof(*pes));
	    pes->stream_id = 0xbc;
	    pes->data = malloc(pklen);
	    pes->hdr = pes->data;
	    pklen = s->stream->read(pes->data, 1, pklen, s->stream);
	    if(pklen < 0)
		return NULL;
	    pes->size = pklen;
	} else if(stream_id != PADDING_STREAM &&
		  stream_id != PRIVATE_STREAM_2 &&
		  stream_id != ECM_STREAM &&
		  stream_id != EMM_STREAM &&
		  stream_id != PROGRAM_STREAM_DIRECTORY &&
		  stream_id != DSMCC_STREAM &&
		  stream_id != H222_E_STREAM &&
		  stream_id != SYSTEM_HEADER){
	    pes = malloc(sizeof(*pes));
	    pes->hdr = malloc(pklen);
	    pklen = s->stream->read(pes->hdr, 1, pklen, s->stream);
	    if(pklen < 0)
		return NULL;
	    pes->stream_id = stream_id;
	    mpegpes_header(pes, pes->hdr, 6);
	    pes->size = pklen - (pes->data - pes->hdr);
	    if(pes->stream_id == PRIVATE_STREAM_1){
		pes->stream_id = *pes->data++;
		pes->data += 3;
		pes->size -= 4;
	    }
	} else {
	    char foo[pklen];
	    if(s->stream->read(foo, 1, pklen, s->stream) < pklen){
		return NULL;
	    }
	}
    } while(!pes);

    return pes;
}

static void
mpegpes_free(mpegpes_packet_t *p)
{
    free(p->hdr);
    free(p);
}

static void
mpegps_free_pk(packet_t *p)
{
    mpegpes_packet_t *mp = p->private;
    mpegpes_free(mp);
    free(p);
}

extern packet_t *
mpegps_packet(muxed_stream_t *ms, int str)
{
    mpegps_stream_t *s = ms->private;
    mpegpes_packet_t *mp = NULL;
    packet_t *pk;
    int sx = -1;

    do {
	if(mp)
	    mpegpes_free(mp);
	if(!(mp = mpegpes_packet(s, 0)))
	    return NULL;
	sx = s->imap[mp->stream_id];
    } while(sx < 0 || !ms->used_streams[sx]);	

    pk = malloc(sizeof(*pk));
    pk->stream = sx;
    pk->data = &mp->data;
    pk->sizes = &mp->size;
    pk->planes = 1;
    pk->flags = 0;
    pk->free = mpegps_free_pk;
    pk->private = mp;

    if(mp->flags & PES_FLAG_PTS){
	pk->pts = mp->pts * 300;
	pk->flags |= TCVP_PKT_FLAG_PTS;
	s->rate = s->stream->tell(s->stream) * 90 / mp->pts;
    }

    if(mp->flags & PES_FLAG_DTS){
	pk->dts = mp->dts * 300;
	pk->flags |= TCVP_PKT_FLAG_DTS;
    }

    return pk;
}

static uint64_t
mpegps_seek(muxed_stream_t *ms, uint64_t time)
{
    mpegps_stream_t *s = ms->private;
    int64_t p, st;
    packet_t *pk = NULL;
    int sm = SEEK_SET, c = 0;

    p = time / 27000 * s->rate;

    do {
	if(s->stream->seek(s->stream, p, sm))
	    return -1;

	st = 0;

	do {
	    pk = mpegps_packet(ms, 0);
	    if(pk){
		if(pk->flags & TCVP_PKT_FLAG_PTS)
		    st = pk->pts;
		pk->free(pk);
	    } else {
		return -1;
	    }
	} while(!st);

	p = ((int64_t) time - st) / 27000 * s->rate;
	sm = SEEK_CUR;
    } while(llabs(st - time) > 27000000 && c++ < 64);

    return st;
}

static void
mpegps_free(void *p)
{
    muxed_stream_t *ms = p;
    mpegps_stream_t *s = ms->private;

    s->stream->close(s->stream);
    free(s->imap);
    free(s);

    mpeg_free(ms);
}

extern muxed_stream_t *
mpegps_open(char *name, conf_section *cs, tcvp_timer_t **tm)
{
    muxed_stream_t *ms;
    mpegps_stream_t *s;
    mpegpes_packet_t *pk = NULL;
    u_char *pm;
    int l, ns, pc = 0;
    stream_t *sp;
    url_t *u;

    if(!(u = url_open(name, "r")))
	return NULL;

    ms = tcallocdz(sizeof(*ms), NULL, mpegps_free);
    ms->next_packet = mpegps_packet;
    ms->seek = mpegps_seek;
    s = calloc(1, sizeof(*s));
    s->stream = u;
    ms->private = s;

    ns = 2;
    ms->streams = calloc(ns, sizeof(*ms->streams));
    s->imap = malloc(0x100 * sizeof(*s->imap));
    memset(s->imap, 0xff, 0x100 * sizeof(*s->imap));
    sp = ms->streams;

    do {
	if(pk)
	    mpegpes_free(pk);
	if(!(pk = mpegpes_packet(s, 1))){
	    break;
	}
    } while(pk->stream_id != 0xbc && pc++ < 16);

    if(pk && pk->stream_id == 0xbc){
	pm = pk->data + 2;
	l = htob_16(unaligned16(pm));
	pm += l + 2;
	l = htob_16(unaligned16(pm));
	pm += 2;

	while(l > 0){
	    u_int stype = *pm++;
	    u_int sid = *pm++;
	    u_int il = htob_16(unaligned16(pm));
	    int sti;

	    pm += 2;

	    if(ms->n_streams == ns){
		ns *= 2;
		ms->streams = realloc(ms->streams, ns * sizeof(*ms->streams));
		sp = &ms->streams[ms->n_streams];
	    }

	    s->imap[sid] = ms->n_streams;

	    if((sti = stream_type2codec(stype)) >= 0){
		memset(sp, 0, sizeof(*sp));
		sp->stream_type = mpeg_stream_types[sti].stream_type;
		sp->common.codec = mpeg_stream_types[sti].codec;
		sp->common.index = ms->n_streams++;
		sp++;
	    }

	    while(il > 0){
		int dl = mpeg_descriptor(sp, pm);
		pm += dl;
		il -= dl;
		l -= dl;
	    }
	    l -= 4;
	}
	mpegpes_free(pk);
    } else {
	if(pk)
	    mpegpes_free(pk);
	s->stream->seek(s->stream, 0, SEEK_SET);
	pc = 0;
	while(pc++ < 128){
	    if(!(pk = mpegpes_packet(s, 1))){
		if(!ms->n_streams){
		    tcfree(ms);
		    return NULL;
		}
		break;
	    }

	    if((pk->stream_id & 0xe0) == 0xc0 ||
	       (pk->stream_id & 0xf0) == 0xe0 ||
	       (pk->stream_id & 0xe0) == 0x80){
		if(s->imap[pk->stream_id] < 0){
		    if(ms->n_streams == ns){
			ns *= 2;
			ms->streams =
			    realloc(ms->streams, ns * sizeof(*ms->streams));
			sp = &ms->streams[ms->n_streams];
		    }

		    s->imap[pk->stream_id] = ms->n_streams;

		    if(pk->stream_id & 0x20){
			sp->stream_type = STREAM_TYPE_VIDEO;
			sp->common.codec = "video/mpeg";
		    } else if(pk->stream_id & 0x40){
			sp->stream_type = STREAM_TYPE_AUDIO;
			sp->common.codec = "audio/mpeg";
		    } else {
			sp->stream_type = STREAM_TYPE_AUDIO;
			sp->common.codec = "audio/ac3";
		    }
		    sp->common.index = ms->n_streams++;
		    sp++;
		}
	    } else {
		fprintf(stderr, "MPEGPS: Unknown stream type %x\n",
			pk->stream_id);
	    }
	    mpegpes_free(pk);
	}
    }

    s->stream->seek(s->stream, 0, SEEK_SET);
    return ms;
}
