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

static mpegpes_packet_t *
mpegpes_packet(mpeg_stream_t *s, int pedantic)
{
    mpegpes_packet_t *pes = NULL;

    do {
	uint32_t stream_id;
	int scode, pklen, zc = 0, i = pedantic? 3: 512;

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
    mpeg_stream_t *s = ms->private;
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

    if(mp->pts_flag){
	pk->pts = (mp->pts * 100) / 9;
	pk->flags |= TCVP_PKT_FLAG_PTS;
    }

    return pk;
}

extern int
mpegps_getinfo(muxed_stream_t *ms)
{
    mpeg_stream_t *s = ms->private;
    mpegpes_packet_t *pk = NULL;
    u_char *pm;
    int l, ns, pc = 0;
    stream_t *sp;
    int err = 0;

    ns = 2;
    ms->streams = calloc(ns, sizeof(*ms->streams));
    s->imap = malloc(0x100 * sizeof(*s->imap));
    memset(s->imap, 0xff, 0x100 * sizeof(*s->imap));
    sp = ms->streams;

    do {
	if(!(pk = mpegpes_packet(s, 1))){
	    break;
	}
    } while(pk->stream_id != 0xbc && pc++ < 16);

    if(pk && pk->stream_id == 0xbc){
	pm = pk->data + 2;
	l = htob_16(unaligned16(pm));
	pm += l;
	l = htob_16(unaligned16(pm));

	while(l > 0){
	    u_int stype = *pm++;
	    u_int sid = *pm++;
	    u_int il = htob_16(unaligned16(pm));
	    pm += 2;

	    if(ms->n_streams == ns){
		ns *= 2;
		ms->streams = realloc(ms->streams, ns * sizeof(*ms->streams));
		sp = &ms->streams[ms->n_streams];
	    }

	    s->imap[sid] = ms->n_streams;

	    if(mpeg_stream_types[stype].stream_type){
		sp->stream_type = mpeg_stream_types[stype].stream_type;
		sp->common.codec = mpeg_stream_types[stype].codec;
		ms->n_streams++;
		sp++;
	    }

	    pm += il;
	    l -= il + 4;
	}	
    } else {
	s->stream->seek(s->stream, 0, SEEK_SET);
	pc = 0;
	while(pc++ < 128){
	    if(!(pk = mpegpes_packet(s, 1))){
		if(!ms->n_streams)
		    err = -1;
		goto out;
	    }
	    if((pk->stream_id & 0xe0) == 0xc0 ||
	       (pk->stream_id & 0xf0) == 0xe0){
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
		    } else {
			sp->stream_type = STREAM_TYPE_AUDIO;
			sp->common.codec = "audio/mpeg";
		    }
		    ms->n_streams++;
		    sp++;
		}
	    } else {
		fprintf(stderr, "MPEGPS: Unknown stream type %x\n",
			pk->stream_id);
	    }
	    mpegpes_free(pk);
	}
    }

out:
    s->stream->seek(s->stream, 0, SEEK_SET);
    return err;
}
