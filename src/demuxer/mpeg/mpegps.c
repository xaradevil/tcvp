/**
    Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

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
#include <stdio.h>
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

typedef struct mpegps_stream {
    url_t *stream;
    int *imap, *map;
    int ps1substr;
    int rate;
    int64_t pts_offset;
    uint64_t time;
    eventq_t qr;
    dvd_functions_t *dvd_info;
    pthread_t eth;
} mpegps_stream_t;

static int TCVP_BUTTON, TCVP_KEY;

static mpegpes_packet_t *
mpegpes_packet(mpegps_stream_t *s, int pedantic)
{
    url_t *u = s->stream;
    mpegpes_packet_t *pes = NULL;

    do {
	uint32_t stream_id;
	u_int scode = 0, pklen, zc = 0, i = pedantic? 3: 0x10000;

	do {
	    scode = url_getc(u);
	    if(scode == 0){
		zc++;
	    } else if(zc >= 2 && scode == 1){
		break;
	    } else if(scode < 0){
		tc2_print("MPEGPS", TC2_PRINT_DEBUG, "zc=%i, scode=%x\n",
			  zc, scode);
		return NULL;
	    } else {
		zc = 0;
	    }
	} while(!scode || i--);

	if(zc < 2 || scode != 1){
	    tc2_print("MPEGPS", TC2_PRINT_DEBUG, "zc=%i, scode=%x\n",
		      zc, scode);
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
	    uint64_t p = u->tell(u);
	    tc2_print("MPEGPS", TC2_PRINT_DEBUG, "end code @ %llu (%llx)\n",
		      p, p);
	    continue;
	    return NULL;
	} else if(stream_id < 0xba){
	    uint64_t p = u->tell(u);
	    tc2_print("MPEGPS", TC2_PRINT_DEBUG,
		      "unknown PES id %x @ %lli (%llx)\n", stream_id, p, p);
	    continue;
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
	    if(pes->stream_id == PRIVATE_STREAM_1 && s->ps1substr){
		pes->stream_id = *pes->data;
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
    tcvp_data_packet_t *pk;
    int sx = -1;

    do {
	if(mp)
	    mpegpes_free(mp);
	if(!(mp = mpegpes_packet(s, 0)))
	    return NULL;

	sx = s->imap[mp->stream_id];

	if((mp->stream_id & 0xf8) == 0x80){
	    mp->data += 4;
	    mp->size -= 4;
	} else if((mp->stream_id & 0xf8) == 0xa0){
	    int aup = htob_16(unaligned16(mp->data + 2));
	    mp->data += 7;
	    mp->size -= 7;
	    if(mp->flags & PES_FLAG_PTS)
		mp->pts -= 27000000LL * aup / ms->streams[sx].common.bit_rate;
	} else if((mp->stream_id & 0xe0) == 0x20){
	    mp->data++;
	    mp->size--;
	} else if(mp->stream_id == DVD_PESID){
	    dvd_event_t *de = (dvd_event_t *) mp->data;
	    tcvp_flush_packet_t *fp;
	    tcvp_still_packet_t *sp;

	    switch(de->type){
	    case DVD_PTSSKIP:
		s->pts_offset = de->ptsskip.offset;
		tc2_print("MPEGPS", TC2_PRINT_DEBUG, "pts offset %lli\n",
			  s->pts_offset);
		break;
	    case DVD_FLUSH:
		fp = tcallocz(sizeof(*fp));
		fp->type = TCVP_PKT_TYPE_FLUSH;
		fp->stream = -1;
		fp->discard = de->flush.drop;
		mpegpes_free(mp);
		return (tcvp_packet_t *) fp;
	    case DVD_STILL:
		sp = tcallocz(sizeof(*sp));
		sp->type = TCVP_PKT_TYPE_STILL;
		sp->stream = 0;
		sp->timeout = 0;
		mpegpes_free(mp);
		return (tcvp_packet_t *) sp;
	    case DVD_AUDIO_ID:
		s->imap[s->map[1]] = -1;
		s->imap[de->audio.id] = 1;
		s->map[1] = de->audio.id;
		break;
	    }
	}
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
get_time(mpegps_stream_t *s)
{
    mpegpes_packet_t *mp;
    uint64_t ts = -1;
    int bc = 0;

    do {
	if(!(mp = mpegpes_packet(s, 0)))
	    break;
	if(mp->flags & PES_FLAG_PTS)
	    ts = mp->pts;
	mpegpes_free(mp);
    } while(ts == -1 && bc++ < 256);

    return ts;
}

#define absdiff(a, b) ((a)>(b)? (a)-(b): (b)-(a))

static uint64_t
mpegps_seek(muxed_stream_t *ms, uint64_t time)
{
    mpegps_stream_t *s = ms->private;
    int64_t p, st, lp, lt, op;
    int64_t tt, d;
    url_t *u = s->stream;

    if(s->dvd_info){
	int idx = time / s->dvd_info->index_unit;
	if(idx > s->dvd_info->index_size)
	    return -1LL;
	else if(idx <= 0)
	    p = 0;
	else
	    p = s->dvd_info->index[idx];
	st = time / 300;
	goto out;
    }

    time /= 300;
    tt = time - 90000;
    if(tt < 0)
	tt = 0;

    p = time * s->rate / 90;
    if(p > u->size || p <= 0)
	p = u->size / 2;
    d = p < u->size / 2? p / 2: (u->size - p) / 2;

    st = lt = get_time(s);
    op = lp = u->tell(u);

    tc2_print("MPEGPS", TC2_PRINT_DEBUG, "seek %lli->%lli, %lli->%lli\n",
	      lt / 90000, tt / 90000, op, p);

    while(absdiff(lt, tt) > 90000 && d > 1000){
	if(u->seek(u, p, SEEK_SET)){
	    tc2_print("MPEGPS", TC2_PRINT_WARNING, "seek failed %lli\n", p);
	    goto err;
	}

	st = get_time(s);
	if(st == -1)
	    goto err;

	p = u->tell(u);

	tc2_print("MPEGPS", TC2_PRINT_DEBUG, "seek %lli @%lli d=%lli\n",
		  st / 90000, p, d);

	if(p == lp)
	    break;

	lp = p;
	lt = st;

	if(st < tt)
	    p += d;
	else
	    p -= d;
	d /= 2;

	if(p > u->size)
	    break;
    }

    while(st < time){
	p = u->tell(u);
	st = get_time(s);
	if(st == -1)
	    goto err;
    }

  out:
    u->seek(u, p, SEEK_SET);
    s->pts_offset = 0;

    tc2_print("MPEGPS", TC2_PRINT_DEBUG, "seek @ %llu (%llx)\n", p, p);

    return st * 300;
  err:
    u->seek(u, op, SEEK_SET);
    return -1;
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
		s->dvd_info->button(s->stream, be->x, be->y);
	} else if(te->type == TCVP_KEY){
	    tcvp_key_event_t *ke = (tcvp_key_event_t *) te;
	    if(!strcmp(ke->key, "escape"))
		s->dvd_info->menu(s->stream);
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
	tcfree(s->stream);
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

static int
mpegps_findpsm(muxed_stream_t *ms, int ns)
{
    mpegps_stream_t *s = ms->private;
    stream_t *sp = ms->streams;
    mpegpes_packet_t *pk = NULL;
    u_char *pm;
    int l, pc = 0;

    do {
	if(pk)
	    mpegpes_free(pk);
	if(!(pk = mpegpes_packet(s, 1))){
	    break;
	}
    } while(pk->stream_id != PROGRAM_STREAM_MAP && pc++ < 16);

    if(!pk || pk->stream_id != PROGRAM_STREAM_MAP){
	if(pk)
	    mpegpes_free(pk);
	return -1;
    }

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
	    sp->common.start_time = -1;

	    if(sid == PRIVATE_STREAM_1)
		s->ps1substr = 0;

	    tc2_print("MPEGPS", TC2_PRINT_DEBUG,
		      "stream %x type %02x\n", sid, stype);

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
    return 0;
}

static int
mpegps_findstreams(muxed_stream_t *ms, int ns)
{
    mpegps_stream_t *s = ms->private;
    stream_t *sp = ms->streams;
    mpegpes_packet_t *pk = NULL;
    int pc = 0;

    while(pc++ < 4096){
	if(!(pk = mpegpes_packet(s, 1))){
	    break;
	}

	if((pk->stream_id & 0xe0) == 0xc0 ||
	   (pk->stream_id & 0xf0) == 0xe0 ||
	   (pk->stream_id & 0xf8) == 0x80 ||
	   (pk->stream_id & 0xf8) == 0xa0 ||
	   (pk->stream_id & 0xe0) == 0x20){
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

		tc2_print("MPEGPS", TC2_PRINT_DEBUG,
			  "found stream id %02x\n", pk->stream_id);

		if((pk->stream_id & 0xf0) == 0xe0){
		    sp->stream_type = STREAM_TYPE_VIDEO;
		    sp->common.codec = "video/mpeg";
		} else if((pk->stream_id & 0xe0) == 0xc0){
		    sp->stream_type = STREAM_TYPE_AUDIO;
		    sp->common.codec = "audio/mpeg";
		} else if((pk->stream_id & 0xf8) == 0x80){
		    sp->stream_type = STREAM_TYPE_AUDIO;
		    sp->common.codec = "audio/ac3";
		} else if((pk->stream_id & 0xf8) == 0xa0){
		    sp->stream_type = STREAM_TYPE_AUDIO;
		    sp->common.codec = "audio/pcm-s16be";
		    if((pk->data[5] & 0x30) == 0){
			sp->audio.sample_rate = 48000;
		    } else if((pk->data[5] & 0x30) == 1){
			sp->audio.sample_rate = 96000;
		    } else {
			tc2_print("MPEGPS", TC2_PRINT_WARNING,
				  "unknown PCM sample rate %i",
				  pk->data[5] & 0x30);
			sp->audio.sample_rate = 48000;
		    }
		    sp->audio.channels = (pk->data[5] & 0x7) + 1;
		    sp->audio.bit_rate =
			sp->audio.channels * sp->audio.sample_rate * 16;
		} else if((pk->stream_id & 0xe0) == 0x20) {
		    sp->stream_type = STREAM_TYPE_SUBTITLE;
		    sp->common.codec = "subtitle/dvd";
		}
		sp->common.index = ms->n_streams++;
		sp->common.start_time = -1;
		sp++;
	    }
	} else {
	    tc2_print("MPEGPS", TC2_PRINT_DEBUG,
		      "unhandled stream id %02x\n", pk->stream_id);
	}
	mpegpes_free(pk);
    }

    return 0;
}

extern muxed_stream_t *
mpegps_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    mpegps_stream_t *s;
    int ns, i;

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

    s->stream = tcref(u);
    s->ps1substr = 1;

    s->dvd_info = tcattr_get(u, "dvd");

    if(s->dvd_info){
	ms->n_streams = s->dvd_info->n_streams;
	ms->streams =
	    realloc(ms->streams, ms->n_streams * sizeof(*ms->streams));
	for(i = 0; i < ms->n_streams; i++){
	    ms->streams[i] = s->dvd_info->streams[i];
	    s->imap[s->dvd_info->streams[i].common.index] = i;
	    s->map[i] = s->dvd_info->streams[i].common.index;
	    ms->streams[i].common.index = i;
	}
    } else if(mpegps_findpsm(ms, ns)){
	u->seek(u, 0, SEEK_SET);
	mpegps_findstreams(ms, ns);
    }

    if(!ms->n_streams){
	u->seek(u, 0, SEEK_SET);
	tcfree(ms);
	return NULL;
    }

    for(i = 0; i < ms->n_streams; i++){
	if(ms->streams[i].stream_type == STREAM_TYPE_SUBTITLE)
	    ms->streams[i].common.flags |= TCVP_STREAM_FLAG_NOBUFFER;
    }

    tc2_print("MPEGPS", TC2_PRINT_DEBUG, "at %x\n",
	      s->stream->tell(s->stream));

    if(!(u->flags & URL_FLAG_STREAMED) && u->size > 1048576 && !s->dvd_info){
	uint64_t stime, etime = -1, tt;
	uint64_t spos, epos;

	u->seek(u, 0, SEEK_SET);
	stime = get_time(s);
	spos = u->tell(u);

	u->seek(u, -1048576, SEEK_END);
	while((tt = get_time(s)) != -1)
	    etime = tt;
	epos = u->tell(u);

	if(stime != -1 && etime != -1){
	    uint64_t dt = etime - stime;
	    uint64_t dp = epos - spos;
	    s->rate = dp * 90 / dt;
	    ms->time = 300LL * dt;
	}
    }

    if(!s->dvd_info)
	s->stream->seek(s->stream, 0, SEEK_SET);

    if(s->dvd_info){
	char *qname, *qn;
	qname = tcvp_event_get_qname(cs);
	qn = alloca(strlen(qname) + 10);
	s->qr = eventq_new(tcref);
	sprintf(qn, "%s/control", qname);
	eventq_attach(s->qr, qn, EVENTQ_RECV);
	free(qname);
	TCVP_BUTTON = tcvp_event_get("TCVP_BUTTON");
	TCVP_KEY = tcvp_event_get("TCVP_KEY");
	pthread_create(&s->eth, NULL, mpegps_event, s);
	s->dvd_info->enable(u, 1);
    }

    return ms;
}
