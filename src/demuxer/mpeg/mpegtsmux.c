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
#include <pthread.h>
#include <tcvp_types.h>
#include <assert.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

#define FIRST_PID 0x100

#define outbuf_size (mux_mpeg_ts_conf_outbuf * 188)

typedef struct mpegts_mux {
    int running;
    url_t *out;
    timer__t **timer;
    int bitrate;
    u_char *outbuf;
    int bpos, bsize;
    uint64_t pcr;
    pthread_mutex_t lock;
    pthread_cond_t cnd;
    int astreams, nstreams;
    struct {
	int pid;
	int stream_id;
	uint64_t pts;
	int ccount;
    } *streams;
    int psic, psifreq;
    u_char *pat;
    u_char *pmt, *pmt_slen;
    u_char *pmap;
    int nextpid;
    pthread_t wth;
} mpegts_mux_t;

static u_char *
pat_packet(int pn, int pmpid)
{
    u_char *tsp = malloc(188);
    u_char *pat = tsp;
    uint32_t crc;

    /* Transport packet header */
    pat[0] = 0x47;
    st_unaligned16(htob_16(0x4000), pat + 1);
    pat[3] = 0x10;
    pat += 4;

    *pat++ = 0;			/* pointer_field */

    /* Program association section */
    pat[0] = 0;
    st_unaligned16(htob_16(0x800d), pat + 1);
    st_unaligned16(0, pat + 3);
    pat[5] = 1;
    pat[6] = 0;			/* section_number */
    pat[7] = 0;			/* last_section */
    st_unaligned16(htob_16(pn), pat + 8);
    st_unaligned16(htob_16(pmpid), pat + 10);

    crc = mpeg_crc32(pat, 12);
    st_unaligned32(htob_32(crc), pat + 12);

    memset(tsp + 21, 0xff, 188 - 21);
    return tsp;
}

static void
init_pmt(mpegts_mux_t *tsm, int pid)
{
    u_char *pmt = malloc(188);
    tsm->pmt = pmt;
    memset(pmt, 0xff, 188);
    
    /* Transport packet header */
    pmt[0] = 0x47;
    st_unaligned16(htob_16(0x4000 | pid), pmt + 1);
    pmt[3] = 0x10;
    pmt += 4;

    *pmt++ = 0;			/* pointer_field */

    /* Program map section */
    pmt[0] = 2;
    pmt[1] = 0x80;
    pmt[2] = 13;
    tsm->pmt_slen = pmt + 2;
    st_unaligned16(htob_16(1), pmt + 3);
    pmt[5] = 1;
    pmt[6] = 0;
    pmt[7] = 0;
    st_unaligned16(htob_16(tsm->nextpid), pmt + 8);
    pmt[10] = 0;
    pmt[11] = 0;
    tsm->pmap = pmt + 12;
}

static void *
tmx_output(void *p)
{
    mpegts_mux_t *tsm = p;
    int op = tsm->bsize / 188;
    uint64_t bytes = 0, pcrb = 0, time;

    while(tsm->running){
	pthread_mutex_lock(&tsm->lock);
	while(tsm->bpos < tsm->bsize && tsm->running)
	    pthread_cond_wait(&tsm->cnd, &tsm->lock);

	if((tsm->out->flags & URL_FLAG_STREAMED) && tsm->bitrate){
	    (*tsm->timer)->wait(*tsm->timer, tsm->pcr);
	    tsm->pcr += (27000000LL * tsm->bpos * 8) / tsm->bitrate;
	}
	time = tsm->pcr - pcrb;
	bytes += tsm->bpos;
	if(time > 27000000 / 2){
	    fprintf(stderr, "%.3lf\r", (double) 8 * bytes * 27000 / time);
	    pcrb = tsm->pcr;
	    bytes = 0;
	}
	if(tsm->out->write(tsm->outbuf, 188, op, tsm->out) != op)
	    tsm->running = 0;
	tsm->bpos = 0;
	pthread_cond_broadcast(&tsm->cnd);
	pthread_mutex_unlock(&tsm->lock);
    }

    return NULL;
}

static int
next_stream(mpegts_mux_t *tsm)
{
    uint64_t pts = -1;
    int s = 0, i;

    for(i = 0; i < tsm->astreams; i++){
	if(tsm->streams[i].pid && tsm->streams[i].pts < pts){
	    pts = tsm->streams[i].pts;
	    s = i;
	}
    }

    return s;
}

static int
tmx_input(tcvp_pipe_t *p, packet_t *pk)
{
    mpegts_mux_t *tsm = p->private;
    char *data;
    int size, str;
    uint64_t pts;
    int unit_start = 1;
    u_char *out;
    int psi = 0;

    if(!pk->data){
	fprintf(stderr, "MPEGTS mux: stream %i end\n", pk->stream);
	pthread_mutex_lock(&tsm->lock);
	if(!--tsm->nstreams)
	    tsm->running = 0;
	tsm->streams[pk->stream].pts = -1LL;
	pthread_cond_broadcast(&tsm->cnd);
	pthread_mutex_unlock(&tsm->lock);
	pk->free(pk);
	return 0;
    }

    data = pk->data[0];
    size = pk->sizes[0];
    str = pk->stream;

    if(pk->flags & TCVP_PKT_FLAG_PTS){
	pthread_mutex_lock(&tsm->lock);
	tsm->streams[str].pts = pk->pts;
	pthread_cond_broadcast(&tsm->cnd);
	pthread_mutex_unlock(&tsm->lock);
    }
    pts = tsm->streams[str].pts;

    while(size > 0){
	int pid = tsm->streams[str].pid;
	int cc = (tsm->streams[str].ccount++ & 0xf) | 0x10;
	int psize = min(size, 184);

	pthread_mutex_lock(&tsm->lock);
	while(tsm->bpos == tsm->bsize || next_stream(tsm) != pk->stream)
	    pthread_cond_wait(&tsm->cnd, &tsm->lock);

	if(!tsm->psic){
	    psi = 2;
	    tsm->psic = tsm->psifreq;
	}

	if(psi > 0){
	    if(--psi){
		memcpy(tsm->outbuf + tsm->bpos, tsm->pat, 188);
		tsm->bpos += 188;
	    }
	    if(tsm->bsize - tsm->bpos >= 188){
		memcpy(tsm->outbuf + tsm->bpos, tsm->pmt, 188);
		tsm->bpos += 188;
		tsm->psic = tsm->psifreq;
		psi--;
	    }
	} else {
	    tsm->psic--;
	    out = tsm->outbuf + tsm->bpos;
	    *out++ = 0x47;
	    if(unit_start)
		pid |= 0x4000;
	    if(unit_start && pk->flags & TCVP_PKT_FLAG_PTS)
		cc |= 0x20;
	    if(psize < 184)		/* stuffing required */
		cc |= 0x20;
	    st_unaligned16(htob_16(pid), out);
	    out += 2;
	    *out++ = cc;
	    if(cc & 0x20){
		int afl = 0;
		int aff = 0;
		int ms;

		if(unit_start && pk->flags & TCVP_PKT_FLAG_PTS){
		    afl += 6;
		    aff |= 0x10;
		}

		ms = 183 - afl - !!afl;
		if(psize < ms){
		    afl = 182 - psize;
		} else {
		    psize = ms;
		}

		if(afl || psize == 182)
		    afl++;

		*out++ = afl;
		if(afl > 0){
		    *out++ = aff;
		    afl--;
		    if(aff & 0x10){
			uint64_t pcrbase = pk->pts / 300 & ((1LL << 33) - 1);
			uint64_t pcrext = pk->pts & 0x1ff;
			st_unaligned32(htob_32(pcrbase >> 1), out);
			out += 4;
			st_unaligned16(htob_16(pcrext | ((pcrbase & 1)<<15)),
				       out);
			out += 2;
			afl -= 6;
		    }
		    while(afl){
			*out++ = 0xff;
			afl--;
		    }
		}
	    }

	    if(unit_start){
		int pesflags = 0;
		int peshl;

		if(pk->flags & TCVP_PKT_FLAG_PTS){
		    pesflags |= PES_FLAG_PTS;
		    if(tsm->pcr < pk->pts){
			tsm->pcr = pk->pts;
			if(tsm->bitrate)
			    tsm->pcr -= (27000000LL * tsm->bpos * 8) /
				tsm->bitrate;
		    }
		}
		peshl = write_pes_header(out, tsm->streams[str].stream_id,
					 size, pesflags, pts / 300);
		
		out += peshl;
		psize -= peshl;
		unit_start = 0;
	    }

	    memcpy(out, data, psize);
	    out += psize;
	    data += psize;
	    size -= psize;
	    tsm->bpos = out - tsm->outbuf;
	    if(tsm->bpos % 188){
		fprintf(stderr, "MPEGTS: BUG: bpos %% 188 != 0\n");
		abort();
	    }
	}
	pthread_cond_broadcast(&tsm->cnd);
	pthread_mutex_unlock(&tsm->lock);
    }

    pk->free(pk);

    return 0;
}

static int
tmx_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    mpegts_mux_t *tsm = p->private;
    mpeg_stream_type_t *str_type = mpeg_stream_type(s->common.codec);
    int pid;
    uint32_t crc;

    if(!str_type)
	return PROBE_FAIL;

    pid = tsm->nextpid++;

    *tsm->pmap++ = str_type->mpeg_stream_type;
    st_unaligned16(htob_16(pid), tsm->pmap);
    tsm->pmap += 2;
    *tsm->pmap++ = 0;
    *tsm->pmap++ = 0;
    *tsm->pmt_slen += 5;

    crc = mpeg_crc32(tsm->pmt_slen - 2, *tsm->pmt_slen - 1);
    st_unaligned32(htob_32(crc), tsm->pmap);

    if(tsm->astreams <= s->common.index){
	int ns = s->common.index + 1;
	tsm->streams = realloc(tsm->streams, ns * sizeof(*tsm->streams));
	memset(tsm->streams + tsm->astreams, 0,
	       (ns - tsm->astreams) * sizeof(*tsm->streams));
	tsm->astreams = ns;
    }

    tsm->streams[s->common.index].pid = pid;
    tsm->streams[s->common.index].stream_id = str_type->stream_id_base;
    tsm->nstreams++;

    return PROBE_OK;
}

static int
tmx_flush(tcvp_pipe_t *p, int drop)
{
    mpegts_mux_t *tsm = p->private;

    pthread_mutex_lock(&tsm->lock);
    if(drop){
	tsm->bpos = 0;
	pthread_cond_broadcast(&tsm->cnd);
    } else {
	while(tsm->bpos)
	    pthread_cond_wait(&tsm->cnd, &tsm->lock);
    }
    pthread_mutex_unlock(&tsm->lock);

    return 0;
}

static void
tmx_free(void *p)
{
    tcvp_pipe_t *tp = p;
    mpegts_mux_t *tsm = tp->private;

    pthread_mutex_lock(&tsm->lock);
    tsm->running = 0;
    pthread_cond_broadcast(&tsm->cnd);
    pthread_mutex_unlock(&tsm->lock);
    pthread_join(tsm->wth, NULL);

    tsm->out->close(tsm->out);
    free(tsm->outbuf);
    free(tsm->pat);
    free(tsm->pmt);
    if(tsm->streams)
	free(tsm->streams);
    pthread_mutex_destroy(&tsm->lock);
    pthread_cond_destroy(&tsm->cnd);
    free(tsm);
}

extern tcvp_pipe_t *
mpegts_new(stream_t *s, conf_section *cs, timer__t **t)
{
    mpegts_mux_t *tsm;
    tcvp_pipe_t *p;
    char *url;
    url_t *out;

    if(conf_getvalue(cs, "mux/url", "%s", &url) <= 0){
	fprintf(stderr, "No output specified.\n");
	return NULL;
    }

    if(!(out = url_open(url, "w"))){
	fprintf(stderr, "Error opening %s.\n", url);
	return NULL;
    }

    tsm = calloc(1, sizeof(*tsm));
    tsm->out = out;
    tsm->outbuf = malloc(outbuf_size);
    tsm->bsize = outbuf_size;
    tsm->bitrate = mux_mpeg_ts_conf_bitrate;
    pthread_mutex_init(&tsm->lock, NULL);
    pthread_cond_init(&tsm->cnd, NULL);
    tsm->nextpid = FIRST_PID;
    tsm->pat = pat_packet(1, tsm->nextpid);
    init_pmt(tsm, tsm->nextpid);
    tsm->nextpid++;
    tsm->running = 1;
    tsm->psifreq = tsm->bitrate / (8 * 188);
    tsm->timer = t;
    pthread_create(&tsm->wth, NULL, tmx_output, tsm);

    p = tcallocdz(sizeof(*p), NULL, tmx_free);
    p->format.stream_type = STREAM_TYPE_MULTIPLEX;
    p->format.common.codec = "mpeg-ts";
    p->input = tmx_input;
    p->probe = tmx_probe;
    p->flush = tmx_flush;
    p->private = tsm;

    return p;
}
