/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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

#define _ISOC99_SOURCE
#define _GNU_SOURCE

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

#define MAX_PACKET_SIZE 0x8000

typedef struct mpegts_packet {
    int transport_error;
    int unit_start;
    int priority;
    int pid;
    int scrambling;
    int adaptation;
    int cont_counter;
    struct adaptation_field {
	int discontinuity;
	int random_access;
	int es_priority;
	int pcr_flag;
	int opcr_flag;
	int splicing_point;
	int transport_private;
	int extension;
	uint64_t pcr;
	uint64_t opcr;
	int splice_countdown;
    } adaptation_field;
    int data_length;
    u_char *datap, data[188];
} mpegts_packet_t;

typedef struct mpegts_stream {
    url_t *stream;
    int bs, br;
    uint32_t bits;
    int *imap;
    struct tsbuf {
	int flags;
	uint64_t pts, dts;
	u_char *buf;
	int bpos;
	int hlen;
	int cc;
	int start;
    } *streams;
    int rate;
    uint64_t start_time;
    tcvp_timer_t *timer;
    int synctime;
} mpegts_stream_t;

typedef struct mpegts_pk {
    packet_t pk;
    u_char *buf, *data;
    int size;
} mpegts_pk_t;

static uint64_t
getbits(mpegts_stream_t *s, int bits, char *name)
{
    uint64_t v = 0;
#ifdef DEBUG
    int _bits = bits;
#endif

    while(bits){
	int b;
	uint32_t m;

	if(!s->bs){
	    int r = s->stream->read(&s->bits, 1, sizeof(s->bits), s->stream);
	    if(r <= 0)
		return -1ULL;
	    s->bits = htob_32(s->bits);
	    s->bs = r * 8;
#ifdef DEBUG
	    fprintf(stderr, "getbits %08x\n", s->bits);
#endif
	}

	b = min(bits, s->bs);
	b = min(b, 8);
	v <<= b;
	m = (1 << b) - 1;
	v |= (s->bits >> (32 - b)) & m;
	s->bits <<= b;
	s->bs -= b;
	s->br += b;
	bits -= b;
    }

#ifdef DEBUG
    fprintf(stderr, "%-24s%9llx [%i]\n", name, v, _bits);
#endif

    return v;
}

static int
mpegts_read_packet(mpegts_stream_t *s, mpegts_packet_t *mp)
{
    int error;

    do {
	int i = 1024;
	int sync = 0;
	error = 0;

	while(--i){
	    sync = getbits(s, 8, "sync");
	    if(sync == MPEGTS_SYNC || sync < 0)
		break;
	}
	if(sync != MPEGTS_SYNC){
	    fprintf(stderr, "MPEGTS: can't find sync byte, @ %llx\n",
		    s->stream->tell(s->stream));
	    return -1;
	}

	s->br = 8;

	mp->transport_error = getbits(s, 1, "transport_error");
	if(mp->transport_error){
	    fprintf(stderr, "MPEGTS: transport error %p\n", s);
	    s->stream->read(mp->data, 1, 188 - (s->br + s->bs) / 8, s->stream);
	    s->bs = 0;
	    error = 1;
	    continue;
	}

	mp->unit_start = getbits(s, 1, "unit_start");
	mp->priority = getbits(s, 1, "priority");
	mp->pid = getbits(s, 13, "pid");
	mp->scrambling = getbits(s, 2, "scrambling");
	mp->adaptation = getbits(s, 2, "adaptation");
	mp->cont_counter = getbits(s, 4, "cont_counter");

	if(mp->adaptation & 2){
	    struct adaptation_field *af = &mp->adaptation_field;
	    int al = getbits(s, 8, "adaptation_field_length");
	    if(al > 0){
		int br = s->br;

		af->discontinuity = getbits(s, 1, "discontinuity");
		af->random_access = getbits(s, 1, "random_access");
		af->es_priority = getbits(s, 1, "es_priority");
		af->pcr_flag = getbits(s, 1, "pcr_flag");
		af->opcr_flag = getbits(s, 1, "opcr_flag");
		af->splicing_point = getbits(s, 1, "splicing_point");
		af->transport_private = getbits(s, 1, "transport_priv_flag");
		af->extension = getbits(s, 1, "af_extention_flag");
		if(af->pcr_flag){
		    uint64_t pcr_base, pcr_ext;
		    pcr_base = getbits(s, 33, "pcr_base");
		    getbits(s, 6, NULL);
		    pcr_ext = getbits(s, 9, "pcr_ext");
		    af->pcr = pcr_base * 300 + pcr_ext;
		}
		if(af->opcr_flag){
		    uint64_t opcr_base, opcr_ext;
		    opcr_base = getbits(s, 33, "opcr_base");
		    getbits(s, 6, NULL);
		    opcr_ext = getbits(s, 9, "opcr_ext");
		    af->opcr = opcr_base * 300 + opcr_ext;
		}
		if(af->splicing_point)
		    af->splice_countdown = getbits(s, 8, "splice_countdown");
		if(af->transport_private){
		    int tl = getbits(s, 8, "private_length");
		    while(tl--){
			getbits(s, 8, "private_data");
		    }
		}
		if(af->extension){
		    int afel = getbits(s, 8, "afext_length");
		    while(afel--){
			getbits(s, 8, "afext_data");
		    }
		}
		br = al - (s->br - br) / 8;
		if(br < 0){
		    fprintf(stderr, "MPEGTS: Bad pack header. br = %i\n", br);
		    error = 1;
		    continue;
		}
		while(br--){
		    if(getbits(s, 8, "stuffing") != 0xff){
			fprintf(stderr, "MPEGTS: Stuffing != 0xff\n");
			error = 1;
			break;
		    }
		}
	    }
	}

	if(!error){
	    if(mp->adaptation & 1){
		mp->data_length = 188 - s->br / 8;
		if(mp->data_length > 184){
		    error = 1;
		    continue;
		}
		for(i = 0; s->bs && i < 184; i++)
		    mp->data[i] = getbits(s, 8, NULL);
		if(i <= mp->data_length){
		    s->stream->read(mp->data+i, 1, mp->data_length-i,
				    s->stream);
		} else {
		    error = 1;
		}
	    } else {
		mp->data_length = 0;
	    }
	    mp->datap = mp->data;
	}
    } while(error);

    return 0;
}

static void
mpegts_free_pk(void *p)
{
    mpegts_pk_t *mp = p;
    free(mp->buf);
}

extern packet_t *
mpegts_packet(muxed_stream_t *ms, int str)
{
    mpegts_stream_t *s = ms->private;
    mpegts_pk_t *pk = NULL;
    mpegts_packet_t mp;
    int sx = -1;
    struct tsbuf *tb;

    do {
	int ccd;

	do {
	    if(mpegts_read_packet(s, &mp) < 0)
		return NULL;
	    sx = s->imap[mp.pid];
	} while(sx < 0 || !ms->used_streams[sx]);

	tb = &s->streams[sx];

	if(tb->cc > -1){
	    ccd = (mp.cont_counter - tb->cc + 0x10) & 0xf;
	    if(ccd == 0){
		fprintf(stderr, "MPEGTS: duplicate packet, PID %x\n", mp.pid);
		continue;
	    } else if(ccd != 1){
		fprintf(stderr, "MPEGTS: lost packet, PID %x: %i %i\n",
			mp.pid, tb->cc, mp.cont_counter);
/* 		tb->start = 0; */
	    }
	}
	tb->cc = mp.cont_counter;

	if((mp.unit_start && tb->bpos) || tb->bpos > MAX_PACKET_SIZE){
	    if(tb->start){
		pk = tcallocdz(sizeof(*pk), NULL, mpegts_free_pk);
		pk->pk.stream = sx;
		pk->pk.data = &pk->data;
		pk->data = tb->buf + tb->hlen;
		pk->buf = tb->buf;
		pk->pk.sizes = &pk->size;
		pk->size = tb->bpos - tb->hlen;
		pk->pk.planes = 1;
		pk->pk.flags = tb->flags;
		if(tb->flags & TCVP_PKT_FLAG_PTS)
		    pk->pk.pts = tb->pts * 300;
		if(tb->flags & TCVP_PKT_FLAG_DTS)
		    pk->pk.dts = tb->dts * 300;
		tb->buf = malloc(0x10000);
	    }
	    tb->bpos = 0;
	    tb->flags = 0;
	    tb->hlen = 0;
	}

	memcpy(tb->buf + tb->bpos, mp.data, mp.data_length);
	tb->bpos += mp.data_length;

	if(mp.unit_start){
	    mpegpes_packet_t pes;
	    if(mpegpes_header(&pes, tb->buf, 0) < 0)
		return NULL;
	    tb->hlen = pes.data - tb->buf;
	    if(pes.flags & PES_FLAG_PTS){
		tb->flags |= TCVP_PKT_FLAG_PTS;
		tb->pts = pes.pts;
/* 		fprintf(stderr, "MPEGTS: %i pts %lli\n", sx, pes.pts * 300); */
	    }
	    if(pes.flags & PES_FLAG_DTS){
		tb->flags |= TCVP_PKT_FLAG_DTS;
		tb->dts = pes.dts;
/* 		fprintf(stderr, "MPEGTS: %i dts %lli\n", sx, pes.dts * 300); */
	    }
	    tb->start = 1;
	}

	if(mp.adaptation_field.pcr_flag){
	    if(s->start_time != -1LL){
		uint64_t time =
		    (mp.adaptation_field.pcr - s->start_time) / 27000;
		if(time)
		    s->rate = s->stream->tell(s->stream) / time;
	    } else {
		s->start_time = mp.adaptation_field.pcr;
	    }

	    if(s->synctime && (s->stream->flags & URL_FLAG_STREAMED) &&
	       s->timer){
		int64_t pcr = mp.adaptation_field.pcr - 27000000 * 1;
		int64_t time = s->timer->read(s->timer);
		int64_t dt;

		if(pcr < 0)
		    pcr += 300LL << 33;
		dt = pcr - time;
		if(llabs(dt) > 270000){
		    time += dt / 2;
		    s->timer->reset(s->timer, time);
		}
	    }
	}
    } while(!pk);

    return &pk->pk;
}

static uint64_t
mpegts_seek(muxed_stream_t *ms, uint64_t time)
{
    mpegts_stream_t *s = ms->private;
    int64_t p, st;
    packet_t *pk = NULL;
    int i, sm = SEEK_SET, c = 0;

    p = time / 27000 * s->rate;

    do {
	p /= 188;
	p *= 188;

	if(s->stream->seek(s->stream, p, sm))
	    return -1;

	for(i = 0; i < ms->n_streams; i++){
	    s->streams[i].flags = 0;
	    s->streams[i].bpos = 0;
	    s->streams[i].start = 0;
	    s->streams[i].cc = -1;
	}

	st = 0;

	do {
	    pk = mpegts_packet(ms, 0);
	    if(pk){
		if(pk->flags & TCVP_PKT_FLAG_PTS)
		    st = pk->pts;
		tcfree(pk);
	    } else {
		return -1;
	    }
	} while(!st);

	p = ((int64_t)time - st) / 27000 * s->rate;
	sm = SEEK_CUR;
    } while(llabs(st - time) > 27000000 && c++ < 64);

    return st;
}

static void
mpegts_free(void *p)
{
    muxed_stream_t *ms = p;
    mpegts_stream_t *s = ms->private;
    int i;

    if(s->stream)
	s->stream->close(s->stream);
    if(s->imap)
	free(s->imap);
    if(s->streams){
	for(i = 0; i < ms->n_streams; i++)
	    free(s->streams[i].buf);
	free(s->streams);
    }
    free(s);
    mpeg_free(ms);
}

extern muxed_stream_t *
mpegts_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    mpegts_stream_t *s;
    mpegts_packet_t mp;
    int seclen, ptr;
    u_char *dp;
    int *pat;
    int i, n, ns, np;
    stream_t *sp;

    int ispmt(int pid){
	int i;
	for(i = 0; i < np; i++){
	    if(pat[2*i+1] == pid){
		int r = pat[2*i];
		pat[2*i] = -1;
		return r;
	    }
	}
	return 0;
    }

    ms = tcallocdz(sizeof(*ms), NULL, mpegts_free);
    ms->next_packet = mpegts_packet;
    ms->seek = mpegts_seek;

    s = calloc(1, sizeof(*s));
    s->stream = tcref(u);
    s->timer = tm;
    tcconf_getvalue(cs, "sync_timer", "%i", &s->synctime);

    ms->private = s;

    do {
	if(mpegts_read_packet(s, &mp) < 0)
	    goto err;
    } while(mp.pid != 0);

    if(!mp.unit_start){
	fprintf(stderr, "MPEGTS: BUG: Large PAT not supported.\n");
	goto err;
    }

    ptr = mp.data[0];
    dp = mp.data + ptr + 1;
    seclen = htob_16(unaligned16(dp + 1)) & 0xfff;
    if(mpeg_crc32(dp, seclen + 3)){
	fprintf(stderr, "MPEGTS: Bad CRC in PAT.\n");
    }
    if(dp[6] || dp[7]){
	fprintf(stderr, "MPEGTS: BUG: Multi-section PAT not supported.\n");
	goto err;
    }

    n = (seclen - 9) / 4;
    pat = calloc(n, 2 * sizeof(*pat));
    dp += 8;
    for(i = 0; i < n; i++){
	pat[2*i] = 1;
	pat[2*i+1] = htob_16(unaligned16(dp + 2)) & 0x1fff;
	fprintf(stderr, "MPEGTS: program %i => PMT pid %x\n",
		htob_16(unaligned16(dp)), pat[i+1]);
	dp += 4;
    }

    np = ns = n;
    ms->streams = calloc(ns, sizeof(*ms->streams));
    s->imap = malloc((1 << 13) * sizeof(*s->imap));
    memset(s->imap, 0xff, (1 << 13) * sizeof(*s->imap));
    sp = ms->streams;

    while(n){
	int pi_len, prg, ip;

	do {
	    if(mpegts_read_packet(s, &mp) < 0)
		goto err;
	} while(!(ip = ispmt(mp.pid)));

	if(ip < 0)
	    break;

	dp = mp.data + mp.data[0] + 1;
	seclen = htob_16(unaligned16(dp + 1)) & 0xfff;
	if(mpeg_crc32(dp, seclen + 3)){
	    fprintf(stderr, "MPEGTS: Bad CRC in PMT.\n");
	    continue;
	}
	prg = htob_16(unaligned16(dp + 3));
	pi_len = htob_16(unaligned16(dp + 10)) & 0xfff;
	dp += 12;

	for(i = 0; i < pi_len;){
	    int tag = dp[0];
	    int tl = dp[1];

	    fprintf(stderr, "MPEGTS: descriptor %i\n", tag);
	    dp += tl + 2;
	    i += tl + 2;
	}

	seclen -= 16 + pi_len;
	for(i = 0; i < seclen;){
	    int stype, epid, esil, sti;
	    int j;

	    if(ms->n_streams == ns){
		ns *= 2;
		ms->streams = realloc(ms->streams, ns * sizeof(*ms->streams));
		sp = &ms->streams[ms->n_streams];
	    }

	    memset(sp, 0, sizeof(*sp));

	    stype = dp[0];
	    epid = htob_16(unaligned16(dp + 1)) & 0x1fff;
	    esil = htob_16(unaligned16(dp + 3)) & 0xfff;
	    dp += 5;
	    i += 5;

	    if((sti = stream_type2codec(stype)) >= 0){
		sp->stream_type = mpeg_stream_types[sti].stream_type;
		sp->common.codec = mpeg_stream_types[sti].codec;
		sp->common.index = ms->n_streams;

		for(j = 0; j < esil;){
		    int tl = mpeg_descriptor(sp, dp);
		    dp += tl;
		    j += tl;
		}

		s->imap[epid] = ms->n_streams++;
		sp++;
	    } else {
		dp += esil;
	    }

	    i += esil;

	    fprintf(stderr, "MPEGTS: program %i => PID %x, type %x\n",
		    prg, epid, stype);
	}

	n--;
    }

    s->streams = calloc(ms->n_streams, sizeof(*s->streams));
    for(i = 0; i < ms->n_streams; i++){
	s->streams[i].buf = malloc(0x10000);
	s->streams[i].cc = -1;
    }

    s->start_time = -1LL;

    free(pat);
    return ms;

err:
    tcfree(ms);
    return NULL;
}
