/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#define _ISOC99_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcbyteswap.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

typedef struct mpegps_stream {
    url_t *stream;
    int *imap, *map;
    int rate;
    int64_t pts_offset;
    uint64_t time;
    eventq_t qr;
    dvd_functions_t *dvd_funcs;
    pthread_t eth;
} mpegps_stream_t;

static int TCVP_BUTTON, TCVP_KEY;

static mpegpes_packet_t *
mpegpes_packet(url_t *u, int pedantic)
{
    mpegpes_packet_t *pes = NULL;

    do {
	uint32_t stream_id;
	int scode = 0, pklen, zc = 0, i = pedantic? 3: 0x10000;

	do {
	    scode = url_getc(u);
	    if(scode == 0){
		zc++;
	    } else if(zc >= 2 && scode == 1){
		break;
	    } else if(scode < 0){
		return NULL;
	    } else {
		zc = 0;
	    }
	} while(!scode || i--);

	if(zc < 2 || scode != 1){
	    return NULL;
	}

	stream_id = url_getc(u);

	if(stream_id == PACK_HEADER){
	    int b = url_getc(u) & 0xc0;
	    char foo[8];
	    if(b == 0x40){
		int sl;
		u->read(foo, 1, 8, u);
		sl = url_getc(u) & 7;
		while(sl--)
		    url_getc(u);
	    } else {
		u->read(foo, 1, 7, u);
	    }
	    continue;
	} else if(stream_id == 0xb9){
	    return NULL;
	}

	pklen = getu16(u);

	if(stream_id == PROGRAM_STREAM_MAP){
	    pes = malloc(sizeof(*pes));
	    pes->stream_id = 0xbc;
	    pes->data = malloc(pklen);
	    pes->hdr = pes->data;
	    pklen = u->read(pes->data, 1, pklen, u);
	    if(pklen < 0)
		return NULL;
	    pes->size = pklen;
	} else if((stream_id & 0xe0) == 0xc0 ||
		  (stream_id & 0xf0) == 0xe0 ||
		  stream_id == PRIVATE_STREAM_1){
	    pes = malloc(sizeof(*pes));
	    pes->hdr = malloc(pklen);
	    pklen = u->read(pes->hdr, 1, pklen, u);
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
	} else if(stream_id == DVD_PESID){
	    pes = malloc(sizeof(*pes));
	    pes->stream_id = stream_id;
	    pes->data = malloc(pklen);
	    pes->hdr = pes->data;
	    u->read(pes->data, 1, pklen, u);
	} else {
	    char foo[pklen];
	    if(u->read(foo, 1, pklen, u) < pklen){
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
mpegps_free_pk(void *v)
{
    packet_t *p = v;
    mpegpes_packet_t *mp = p->private;
    mpegpes_free(mp);
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
	if(!(mp = mpegpes_packet(s->stream, 0)))
	    return NULL;
	if(mp->stream_id == DVD_PESID){
	    dvd_event_t *de = (dvd_event_t *) mp->data;
	    switch(de->type){
	    case DVD_PTSSKIP:
		s->pts_offset = de->ptsskip.offset;
		fprintf(stderr, "MPEGPS: pts offset %lli\n",
			s->pts_offset);
		break;
	    case DVD_FLUSH:
		pk = tcallocdz(sizeof(*pk), NULL, mpegps_free_pk);
		pk->type = TCVP_PKT_TYPE_FLUSH;
		pk->stream = de->flush.drop;
		pk->private = mp;
		return pk;
	    case DVD_STILL:
		pk = tcallocdz(sizeof(*pk), NULL, mpegps_free_pk);
		pk->type = TCVP_PKT_TYPE_STILL;
		pk->private = mp;
		return pk;
	    case DVD_AUDIO_ID:
		s->imap[s->map[1]] = -1;
		s->imap[de->audio.id] = 1;
		s->map[1] = de->audio.id;
		break;
	    }
	    continue;
	}

	sx = s->imap[mp->stream_id];
    } while(sx < 0 || !ms->used_streams[sx]);	

    pk = tcallocdz(sizeof(*pk), NULL, mpegps_free_pk);
    pk->stream = sx;
    pk->data = &mp->data;
    pk->sizes = &mp->size;
    pk->planes = 1;
    pk->flags = 0;
    pk->private = mp;

    if(mp->flags & PES_FLAG_PTS){
	mp->pts += s->pts_offset;
	mp->dts += s->pts_offset;
	pk->pts = mp->pts * 300;
	pk->flags |= TCVP_PKT_FLAG_PTS;
	if(mp->pts)
	    s->rate = s->stream->tell(s->stream) * 90 / mp->pts;
	s->time = mp->pts;
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
    mpegpes_packet_t *pk = NULL;
    int c = 0;
    int64_t p = 0, st = 0, bin = 0, d = 0;

    time /= 300;

    do {
	if(bin){
	    if(time > st)
		d = (int64_t) s->stream->size / bin;
	    else
		d = -(int64_t) s->stream->size / bin;
	    p += d;
	    bin <<= 1;
	    if(bin <= 0)
		break;
	} else {
	    p = time * s->rate / 90;
	}

	if(p > s->stream->size){
	    p = s->stream->size / 2;
	    bin = 4;
	}

	if(s->stream->seek(s->stream, p, SEEK_SET)){
	    fprintf(stderr, "seek failed, p = %lli\n", p);
	    return -1;
	}

	st = 0;

	do {
	    pk = mpegpes_packet(s->stream, 0);
	    if(pk){
		if(pk->flags & PES_FLAG_PTS)
		    st = pk->pts;
		mpegpes_free(pk);
	    } else {
		return -1;
	    }
	} while(!st);
	s->rate = s->stream->tell(s->stream) * 90 / st;
    } while(llabs(st - time) > 90000 && c++ < 512);

    s->pts_offset = 0;

    return st * 300;
}

static void *
mpegps_event(void *p)
{
    mpegps_stream_t *s = p;
    int run = 1;

    while(run){
	tcvp_event_t *te = eventq_recv(s->qr);
	if(te->type == TCVP_BUTTON){
	    tcvp_button_event_t *be = (tcvp_button_event_t *) te;
	    if(be->button == 1 &&
	       be->action == TCVP_BUTTON_PRESS)
		s->dvd_funcs->button(s->stream, be->x, be->y);
	} else if(te->type == TCVP_KEY){
	    tcvp_key_event_t *ke = (tcvp_key_event_t *) te;
	    if(!strcmp(ke->key, "escape"))
		s->dvd_funcs->menu(s->stream);
	} else if(te->type == -1){
	    run = 0;
	}
	tcfree(te);
    }

    return NULL;
}

static void
mpegps_free(void *p)
{
    muxed_stream_t *ms = p;
    mpegps_stream_t *s = ms->private;

    if(s->stream)
	s->stream->close(s->stream);
    free(s->imap);
    free(s->map);

    if(s->qr){
	tcvp_event_send(s->qr, -1);
	pthread_join(s->eth, NULL);
	eventq_delete(s->qr);
    }
    free(s);

    mpeg_free(ms);
}

extern muxed_stream_t *
mpegps_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    mpegps_stream_t *s;
    mpegpes_packet_t *pk = NULL;
    uint64_t bt = 0;
    int bc = 0;
    u_char *pm;
    int l, ns, pc = 0;
    stream_t *sp;

    ms = tcallocdz(sizeof(*ms), NULL, mpegps_free);
    ms->next_packet = mpegps_packet;
    ms->seek = mpegps_seek;
    s = calloc(1, sizeof(*s));
    ms->private = s;

    ns = 2;
    ms->streams = calloc(ns, sizeof(*ms->streams));
    s->imap = malloc(0x100 * sizeof(*s->imap));
    memset(s->imap, 0xff, 0x100 * sizeof(*s->imap));
    s->map = malloc(0x100 * sizeof(*s->map));
    memset(s->map, 0xff, 0x100 * sizeof(*s->map));
    sp = ms->streams;

    do {
	if(pk)
	    mpegpes_free(pk);
	if(!(pk = mpegpes_packet(u, 1))){
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
	    s->map[ms->n_streams] = sid;

	    if((sti = stream_type2codec(stype)) >= 0){
		memset(sp, 0, sizeof(*sp));
		sp->stream_type = mpeg_stream_types[sti].stream_type;
		sp->common.codec = mpeg_stream_types[sti].codec;
		sp->common.index = ms->n_streams++;

		while(il > 0){
		    int dl = mpeg_descriptor(sp, pm);
		    pm += dl;
		    il -= dl;
		    l -= dl;
		}

		sp++;
	    } else {
		pm += il;
		l -= il;
	    }
	    l -= 4;
	}
	mpegpes_free(pk);
    } else {
	if(pk)
	    mpegpes_free(pk);
	u->seek(u, 0, SEEK_SET);
	pc = 0;
	while(pc++ < 128){
	    if(!(pk = mpegpes_packet(u, 1))){
		if(!ms->n_streams){
		    u->seek(u, 0, SEEK_SET);
		    tcfree(ms);
		    return NULL;
		}
		break;
	    }

	    if((pk->stream_id & 0xe0) == 0xc0 ||
	       (pk->stream_id & 0xf0) == 0xe0 ||
	       (pk->stream_id & 0xf8) == 0x80){
		if(s->imap[pk->stream_id] < 0){
		    if(ms->n_streams == ns){
			ns *= 2;
			ms->streams =
			    realloc(ms->streams, ns * sizeof(*ms->streams));
			sp = &ms->streams[ms->n_streams];
		    }

		    memset(sp, 0, sizeof(*sp));
		    s->imap[pk->stream_id] = ms->n_streams;
		    s->map[ms->n_streams] = pk->stream_id;

		    fprintf(stderr, "MPEGPS: found stream id %02x\n",
			    pk->stream_id);

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
	    }
	    mpegpes_free(pk);
	}
    }

    if(!(u->flags & URL_FLAG_STREAMED))
	u->seek(u, u->size / 1024, SEEK_SET);

    do {
	if(!(pk = mpegpes_packet(u, 0)))
	    break;
	if(pk->flags & PES_FLAG_PTS)
	    bt = pk->pts;
	mpegpes_free(pk);
    } while(bt < 90000 && bc++ < 256);

    if(bt > 0){
	uint64_t sp = u->tell(u);
	s->rate = sp * 90 / bt;
	ms->time = 300LL * u->size / sp * bt;
    }

    s->stream = tcref(u);
    s->stream->seek(s->stream, 0, SEEK_SET);

    s->dvd_funcs = tcattr_get(u, "dvd");
    if(s->dvd_funcs){
	char *qname, *qn;
	tcconf_getvalue(cs, "qname", "%s", &qname);
	qn = alloca(strlen(qname) + 10);
	s->qr = eventq_new(tcref);
	sprintf(qn, "%s/control", qname);
	eventq_attach(s->qr, qn, EVENTQ_RECV);
	free(qname);
	TCVP_BUTTON = tcvp_event_get("TCVP_BUTTON");
	TCVP_KEY = tcvp_event_get("TCVP_KEY");
	pthread_create(&s->eth, NULL, mpegps_event, s);
	s->dvd_funcs->enable(u, 1);
    }

    return ms;
}
