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
#include <tclist.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <sys/time.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

#define FIRST_PID 0x100

#define outbuf_size (mux_mpeg_ts_conf_outbuf * 188)

typedef struct mpegts_mux {
    int running;
    url_t *out;
    tcvp_timer_t *timer;
    int bitrate, pad;
    u_char *outbuf;
    int bpos, bsize;
    uint64_t pcr, pcr_int, last_pcr;
    uint64_t time;
    int pcr_pid;
    pthread_mutex_t lock;
    pthread_cond_t cnd;
    int astreams, nstreams, rstreams;
    struct mpegts_output_stream {
	int stream_type;
	int pid;
	int stream_id;
	uint64_t dts, sdts;
	int ccount;
	int bitrate;
	int bytes;
	uint64_t bdts;
	uint64_t lpts;
    } *streams;
    int psifreq;
    uint64_t psipcr;
    u_char *pat;
    u_char *pmt, *pmt_slen;
    u_char *pmap;
    u_char *null;
    int nextpid;
    pthread_t wth;
    uint64_t start_time;
    int64_t tbytes, padbytes;
    int realtime;
    double rate_factor;
    int64_t pcr_delay;
    uint64_t pts_interval;
} mpegts_mux_t;

static u_char *
null_packet(void)
{
    u_char *null = malloc(188);

    null[0] = 0x47;
    null[1] = 0x1f;
    null[2] = 0xff;
    null[3] = 0x10;
    memset(null + 4, 0, 184);

    return null;
}

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
    st_unaligned16(htob_16(0xb00d), pat + 1);
    st_unaligned16(0, pat + 3);
    pat[5] = 0xc1;
    pat[6] = 0;			/* section_number */
    pat[7] = 0;			/* last_section */
    st_unaligned16(htob_16(pn), pat + 8);
    st_unaligned16(htob_16(pmpid | 0xe000), pat + 10);

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
    pmt[1] = 0xb0;
    pmt[2] = 13;
    tsm->pmt_slen = pmt + 2;
    st_unaligned16(htob_16(1), pmt + 3);
    pmt[5] = 0xc1;
    pmt[6] = 0;
    pmt[7] = 0;
    pmt[10] = 0;
    pmt[11] = 0;
    tsm->pmap = pmt + 12;
}

static void
inc_cc(u_char *p)
{
    int cc = p[3];
    cc = (cc & 0xf0) | ((cc + 1) & 0x0f);
    p[3] = cc;
}

static void
put_pcr(u_char *p, uint64_t pcr)
{
    uint64_t pcrbase = pcr / 300 & ((1LL << 33) - 1);
    uint64_t pcrext = (pcr % 300) & 0x1ff;
    st_unaligned32(htob_32(pcrbase >> 1), p);
    st_unaligned16(htob_16(pcrext | ((pcrbase & 1)<<15) | 0x7e00), p + 4);
}

static int
next_stream(mpegts_mux_t *tsm)
{
    uint64_t pts = -1;
    int s = 0, i;

    for(i = 0; i < tsm->astreams; i++){
	if(tsm->streams[i].pid && tsm->streams[i].dts < pts){
	    pts = tsm->streams[i].dts;
	    s = i;
	}
    }
    return s;
}

static int
write_ts_packet(mpegts_mux_t *tsm, int str, u_char *data, size_t size, 
		int ustart, uint64_t pcr, uint64_t pts, uint64_t dts)
{
    struct mpegts_output_stream *os = tsm->streams + str;
    int cc = (os->ccount++ & 0xf) | 0x10;
    int psize = min(size, 184);
    int pid = os->pid;
    u_char *out = tsm->outbuf + tsm->bpos;

    *out++ = 0x47;

    if(ustart)
	pid |= 0x4000;

    if(ustart && pts != -1)
	cc |= 0x20;
    if(pcr != -1)
	cc |= 0x20;
    if(psize < 184)		/* stuffing required */
	cc |= 0x20;

    st_unaligned16(htob_16(pid), out);
    out += 2;
    *out++ = cc;

    if(cc & 0x20){		/* adaptation field */
	int afl = 0;
	int aff = 0;
	int ms;

	if(pcr != -1){
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
		put_pcr(out, pcr);
		out += 6;
		afl -= 6;
	    }
	    while(afl){
		*out++ = 0xff;
		afl--;
	    }
	}
    }

    if(ustart){
	int pesflags = 0, pessize;
	int peshl;

	if(pts != -1){
	    pesflags |= PES_FLAG_PTS;
	    if(dts != -1)
		pesflags |= PES_FLAG_DTS;
	}

	pessize = os->stream_type == STREAM_TYPE_VIDEO? 0: size;
	peshl = write_pes_header(out, os->stream_id,
				 pessize, pesflags, pts / 300,
				 dts / 300);
	out += peshl;
	psize -= peshl;
    }

    memcpy(out, data, psize);
    tsm->bpos += 188;

    return psize;
}

static int
tmx_input(tcvp_pipe_t *p, packet_t *pk)
{
    mpegts_mux_t *tsm = p->private;
    struct mpegts_output_stream *os;
    char *data;
    int size;
    int64_t dts;
    int unit_start = 1;
    uint64_t ppts = -1, pdts = -1, pcr = -1;
    int psi = 0;

    if(!pk->data){
	tc2_print("MPEGTS-MUX", TC2_PRINT_DEBUG,
		  "stream %i end\n", pk->stream);
	pthread_mutex_lock(&tsm->lock);
	if(!--tsm->nstreams)
	    tsm->running = 0;
	tsm->streams[pk->stream].dts = -1LL;
	pthread_cond_broadcast(&tsm->cnd);
	pthread_mutex_unlock(&tsm->lock);
	tcfree(pk);
	return 0;
    }

    data = pk->data[0];
    size = pk->sizes[0];
    os = tsm->streams + pk->stream;

    os->bytes += size;

    if(pk->flags & TCVP_PKT_FLAG_PTS){
	uint64_t bdts = os->bdts?: tsm->start_time;
	int br;

	if(pk->flags & TCVP_PKT_FLAG_DTS)
	    dts = pk->dts;
	else
	    dts = pk->pts;

	if(dts - os->lpts > tsm->pts_interval){
	    if(pk->flags & TCVP_PKT_FLAG_PTS)
		ppts = pk->pts;
	    if(pk->flags & TCVP_PKT_FLAG_DTS)
		pdts = pk->dts;
	}

	if(dts - bdts > tcvp_demux_mpeg_conf_ts_rate_window * 27000LL){
	    br = (27000000LL * os->bytes * 8) / (dts - bdts);
	    os->bdts = dts;
	    os->bytes = 0;
	    os->bitrate = br;
	    tc2_print("MPEGTS", TC2_PRINT_DEBUG+1 + pk->stream,
		      "[%i] bitrate %i\n", pk->stream, br);
	}

	pthread_mutex_lock(&tsm->lock);
	os->dts = dts;
	pthread_cond_broadcast(&tsm->cnd);
	pthread_mutex_unlock(&tsm->lock);
    } else {
	dts = os->dts;
    }

    if(os->sdts - tsm->last_pcr > tsm->pcr_int && os->pid == tsm->pcr_pid){
	pcr = os->sdts;
	tsm->last_pcr = pcr;
    }

    os->sdts = dts;

    if(dts - tsm->psipcr > tsm->psifreq && os->pid == tsm->pcr_pid &&
       tsm->nstreams == tsm->rstreams){
	tc2_print("MPEGTS", TC2_PRINT_DEBUG, "sending PSI %llu\n",
		  tsm->psipcr);
	psi = 2;
	tsm->psipcr = dts;
    }

    while(size > 0){
	pthread_mutex_lock(&tsm->lock);

	if(tsm->realtime){
	    tsm->timer->wait(tsm->timer, dts + 27000000, &tsm->lock);
	} else {
	    while(next_stream(tsm) != pk->stream)
		pthread_cond_wait(&tsm->cnd, &tsm->lock);
	}

	if(psi > 0){
	    if(--psi){
		memcpy(tsm->outbuf + tsm->bpos, tsm->pat, 188);
		inc_cc(tsm->pat);
		tsm->bpos += 188;
	    }
	    if(tsm->bsize - tsm->bpos >= 188){
		memcpy(tsm->outbuf + tsm->bpos, tsm->pmt, 188);
		inc_cc(tsm->pmt);
		tsm->bpos += 188;
		psi--;
	    }
	} else {
	    int psize = write_ts_packet(tsm, pk->stream, data, size,
					unit_start, pcr, ppts, pdts);
	    unit_start = 0;
	    pcr = -1;
	    data += psize;
	    size -= psize;
	    dts += 27000000LL * 8 * 188 * tsm->rate_factor /
		(os->bitrate?: tsm->bitrate);
	    os->dts = dts;
	}

	if(tsm->bpos == tsm->bsize){
	    tsm->out->write(tsm->outbuf, 1, tsm->bpos, tsm->out);
	    tsm->bpos = 0;
	}

	pthread_cond_broadcast(&tsm->cnd);
	pthread_mutex_unlock(&tsm->lock);
    }

    tcfree(pk);

    return 0;
}

static int
tmx_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    mpegts_mux_t *tsm = p->private;
    mpeg_stream_type_t *str_type = mpeg_stream_type(s->common.codec);
    int pid;
    uint32_t crc;
    int esil = 0;
    u_char *ilp;

    if(!str_type){
	tc2_print("MPEGTS", TC2_PRINT_ERROR, "unsupported codec %s\n",
		  s->common.codec);
	return PROBE_FAIL;
    }

    pthread_mutex_lock(&tsm->lock);

    pid = tsm->nextpid++;

    tc2_print("MPEGTS", TC2_PRINT_DEBUG, "new stream %i, pid %x, type %x\n",
	      s->common.index, pid, str_type->mpeg_stream_type);

    *tsm->pmap++ = str_type->mpeg_stream_type;
    st_unaligned16(htob_16(pid | 0xe000), tsm->pmap);
    tsm->pmap += 2;
    ilp = tsm->pmap;
    tsm->pmap += 2;
    *tsm->pmt_slen += 5;

    if(!tsm->pcr_pid && s->stream_type == STREAM_TYPE_VIDEO){
	tsm->pcr_pid = pid;
	st_unaligned16(htob_16(pid | 0xe000), tsm->pmt + 13);
    }

    if(s->stream_type == STREAM_TYPE_VIDEO){
	int l;
	l = write_mpeg_descriptor(s, VIDEO_STREAM_DESCRIPTOR,
				  tsm->pmap, 181 - *tsm->pmt_slen);
	tsm->pmap += l;
	esil += l;

	l = write_mpeg_descriptor(s, TARGET_BACKGROUND_GRID_DESCRIPTOR,
				  tsm->pmap, 181 - *tsm->pmt_slen);
	tsm->pmap += l;
	esil += l;
    }

    st_unaligned16(htob_16(esil | 0xf000), ilp);
    *tsm->pmt_slen += esil;

    crc = mpeg_crc32(tsm->pmt_slen - 2, *tsm->pmt_slen - 1);
    st_unaligned32(htob_32(crc), tsm->pmap);

    if(tsm->astreams <= s->common.index){
	int ns = s->common.index + 1;
	tsm->streams = realloc(tsm->streams, ns * sizeof(*tsm->streams));
	memset(tsm->streams + tsm->astreams, 0,
	       (ns - tsm->astreams) * sizeof(*tsm->streams));
	tsm->astreams = ns;
    }

    tsm->streams[s->common.index].stream_type = s->stream_type;
    tsm->streams[s->common.index].pid = pid;
    tsm->streams[s->common.index].stream_id = str_type->stream_id_base;
    tsm->streams[s->common.index].bitrate = s->common.bit_rate?: 320000;
    tsm->streams[s->common.index].lpts = 1LL << 63;
    tsm->nstreams++;

    if(s->common.start_time < tsm->start_time)
	tsm->start_time = s->common.start_time;

    pthread_mutex_unlock(&tsm->lock);
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
	if(tsm->bpos){
	    tsm->out->write(tsm->outbuf, 1, tsm->bpos, tsm->out);
	    tsm->bpos = 0;
	}
    }
    pthread_mutex_unlock(&tsm->lock);

    return 0;
}

static void
tmx_ref(void *p)
{
    tcvp_pipe_t *tp = p;
    mpegts_mux_t *tsm = tp->private;

    pthread_mutex_lock(&tsm->lock);
    tsm->rstreams++;
    tc2_print("MPEGTS", TC2_PRINT_DEBUG, "ref %i\n", tsm->rstreams);
    pthread_mutex_unlock(&tsm->lock);
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
/*     pthread_join(tsm->wth, NULL); */

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
mpegts_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	   muxed_stream_t *ms)
{
    mpegts_mux_t *tsm;
    tcvp_pipe_t *p;
    char *url;
    url_t *out;

    if(tcconf_getvalue(cs, "mux/url", "%s", &url) <= 0){
	tc2_print("MPEGTS-MUX", TC2_PRINT_ERROR, "No output specified.\n");
	return NULL;
    }

    if(!(out = url_open(url, "w"))){
	tc2_print("MPEGTS-MUX", TC2_PRINT_ERROR, "Error opening %s.\n", url);
	return NULL;
    }

    tsm = calloc(1, sizeof(*tsm));
    tsm->out = out;
    tsm->outbuf = malloc(outbuf_size);
    tsm->bsize = outbuf_size;
    tsm->bitrate = mux_mpeg_ts_conf_bitrate;
    tsm->pad = mux_mpeg_ts_conf_pad;
    pthread_mutex_init(&tsm->lock, NULL);
    pthread_cond_init(&tsm->cnd, NULL);
    tsm->nextpid = FIRST_PID;
    tsm->pat = pat_packet(1, tsm->nextpid);
    init_pmt(tsm, tsm->nextpid);
    tsm->nextpid++;
    tsm->running = 1;
    tsm->psifreq = 27000000;
    tsm->timer = t;
    tsm->null = null_packet();
    tsm->pcr_int = 27000 * mux_mpeg_ts_conf_pcr_interval * 3 / 4;
    tsm->start_time = -1;
    tsm->realtime = out->flags & URL_FLAG_STREAMED;
    tsm->rate_factor = tcvp_demux_mpeg_conf_ts_rate_factor;
    tsm->pcr_delay = tcvp_demux_mpeg_conf_pcr_delay;
    tsm->pts_interval = tcvp_demux_mpeg_conf_pts_interval;
    tsm->rstreams = 1;

    tcconf_getvalue(cs, "bitrate", "%i", &tsm->bitrate);
    tcconf_getvalue(cs, "pad", "%i", &tsm->pad);
    tcconf_getvalue(cs, "realtime", "%i", &tsm->realtime);
    tcconf_getvalue(cs, "rate_factor", "%lf", &tsm->rate_factor);
    tcconf_getvalue(cs, "pcr_delay", "%li", &tsm->pcr_delay);
    tcconf_getvalue(cs, "pts_interval", "%li", &tsm->pts_interval);
    tcconf_getvalue(cs, "psi_interval", "%i", &tsm->psifreq);

    tsm->pcr_delay *= 27000;
    tsm->pts_interval *= 27000;

    p = tcallocdz(sizeof(*p), tmx_ref, tmx_free);
    p->format.stream_type = STREAM_TYPE_MULTIPLEX;
    p->format.common.codec = "mpeg-ts";
    p->format.common.bit_rate = tsm->bitrate;
    p->input = tmx_input;
    p->probe = tmx_probe;
    p->flush = tmx_flush;
    p->private = tsm;

    free(url);

    return p;
}
