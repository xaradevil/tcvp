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

static uint64_t
getbits(mpeg_stream_t *s, int bits, char *name)
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

static mpegts_packet_t *
mpegts_read_packet(mpeg_stream_t *s)
{
    mpegts_packet_t *mp;
    int error;

    mp = malloc(sizeof(*mp));

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
	    free(mp);
	    fprintf(stderr, "MPEGTS: can't find sync byte, @ %lx\n",
		    s->stream->tell(s->stream));
	    return NULL;
	}

	s->br = 8;

	mp->transport_error = getbits(s, 1, "transport_error");
	if(mp->transport_error){
	    fprintf(stderr, "MPEGTS: transport error\n");
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
		for(i = 0; s->bs; i++)
		    mp->data[i] = getbits(s, 8, NULL);
		s->stream->read(mp->data+i, 1, mp->data_length-i, s->stream);
	    } else {
		mp->data_length = 0;
	    }
	    mp->datap = mp->data;
	}
    } while(error);

    return mp;
}

static void
mpegts_free_pk(packet_t *p)
{
    mpegts_packet_t *mp = p->private;
    free(mp);
    free(p);
}

extern packet_t *
mpegts_packet(muxed_stream_t *ms, int str)
{
    mpeg_stream_t *s = ms->private;
    packet_t *pk = NULL;
    mpegts_packet_t *mp;

    int sx = -1;

    do {
	mp = mpegts_read_packet(s);

	if(!mp)
	    return NULL;

	sx = s->imap[mp->pid];

	if(sx < 0 || !ms->used_streams[sx])
	    free(mp);
    } while(sx < 0 || !ms->used_streams[sx]);

    pk = malloc(sizeof(*pk));
    pk->stream = sx;
    pk->data = &mp->datap;
    pk->sizes = &mp->data_length;
    pk->planes = 1;
    pk->flags = 0;
    pk->free = mpegts_free_pk;
    pk->private = mp;

    if(mp->unit_start){
	mpegpes_packet_t pes;
	int hlen;

	mpegpes_header(&pes, mp->datap, 0);
	hlen = pes.data - mp->datap;
	if(pes.pts_flag){
	    uint64_t upts = pes.pts;
	    upts *= 100ULL;
	    upts /= 9ULL;
	    if(pes.pts < s->pts[sx] &&
	       s->pts[sx] - pes.pts > 24000){
		fprintf(stderr, "MPEGTS: PTS discontinuous\n");
	    }
	    s->pts[sx] = pes.pts;
	    pk->pts = upts;
	    pk->flags |= TCVP_PKT_FLAG_PTS;
	}

	mp->datap += hlen;
	mp->data_length -= hlen;
    }

    return pk;
}

extern int
mpegts_getinfo(muxed_stream_t *ms)
{
    mpeg_stream_t *s = ms->private;
    mpegts_packet_t *mp = NULL;
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

    do {
	if(mp)
	    free(mp);
	if(!(mp = mpegts_read_packet(s)))
	    return -1;
    } while(mp->pid != 0);

    if(!mp->unit_start){
	fprintf(stderr, "MPEGTS: BUG: Large PAT not supported.\n");
	return -1;
    }

    ptr = mp->data[0];
    dp = mp->data + ptr + 1;
    seclen = htob_16(unaligned16(dp + 1)) & 0xfff;
    if(dp[6] || dp[7]){
	fprintf(stderr, "MPEGTS: BUG: Multi-section PAT not supported.\n");
	return -1;
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
	    if(mp)
		free(mp);
	    if(!(mp = mpegts_read_packet(s)))
		return -1;
	} while(!(ip = ispmt(mp->pid)));

	if(ip < 0)
	    break;

	dp = mp->data + mp->data[0] + 1;
	seclen = htob_16(unaligned16(dp + 1)) & 0xfff;
	prg = htob_16(unaligned16(dp + 3));
	pi_len = htob_16(unaligned16(dp + 10)) & 0xfff;
	dp += 12 + pi_len;
	seclen -= 16 + pi_len;
	for(i = 0; i < seclen;){
	    int stype, epid, esil;

	    if(ms->n_streams == ns){
		ns *= 2;
		ms->streams = realloc(ms->streams, ns * sizeof(*ms->streams));
		sp = &ms->streams[ms->n_streams];
	    }

	    stype = dp[0];
	    epid = htob_16(unaligned16(dp + 1)) & 0x1fff;
	    esil = htob_16(unaligned16(dp + 3)) & 0xfff;
	    dp += 5 + esil;
	    i += 5 + esil;

	    s->imap[epid] = ms->n_streams;

	    fprintf(stderr, "MPEGTS: program %i => PID %x, type %x\n",
		    prg, epid, stype);

	    if(mpeg_stream_types[stype].stream_type){
		sp->stream_type = mpeg_stream_types[stype].stream_type;
		sp->common.codec = mpeg_stream_types[stype].codec;
		ms->n_streams++;
		sp++;
	    }
	}

	n--;
    }

    free(pat);
    free(mp);

    return 0;
}
