/**
    Copyright (C) 2003-2005  Michael Ahlberg, Måns Rullgård

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
#include <tcvp_types.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

#define TS_PACKET_SIZE 188
#define outbuf_size (mux_mpeg_ts_conf_outbuf * 188)

typedef struct mpegts_mux {
    url_t *out;
    tcvp_timer_t *timer;
    u_char *outbuf;
    int bpos, bsize;
    uint64_t pcr, pcr_int, last_pcr;
    uint64_t last_psi;
    int psi_interval;
    int discont;
    int bitrate;
    int astreams;
    struct mpegts_output_stream {
	int stream_type;
	int pid;
	int stream_id;
	uint64_t dts;
	uint64_t sts;
	int ccount;
	int bitrate;
	int bytes;
	uint64_t lpts;
	int unit_start;
	tclist_t *packets;
	uint64_t tailtime;
	uint64_t headtime;
	int rbytes;
	int64_t offset;
    } *streams;
    u_char *pat;
    u_char *pmt, *pmt_slen;
    u_char *pmap;
    u_char *pcr_packet;
    u_char *null;
    int pcr_pid;
    int nextpid;
    uint64_t start_time;
    int realtime;
    int64_t delay;
    int64_t pcr_offset;
    uint64_t pts_interval;
    int started;
    uint64_t bytes;
    uint64_t rate_lookahead;
    int64_t audio_offset;
    int64_t video_offset;
    double padding;
    int audio_rate;
    int video_rate;
} mpegts_mux_t;

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

static u_char *
null_packet(void)
{
    u_char *tsp = calloc(1, TS_PACKET_SIZE);

    tsp[0] = 0x47;
    st_unaligned16(htob_16(0x1fff), tsp + 1);
    tsp[3] = 0x10;

    return tsp;
}

static u_char *
pat_packet(int pn, int pmpid)
{
    u_char *tsp = malloc(TS_PACKET_SIZE);
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

    memset(tsp + 21, 0xff, TS_PACKET_SIZE - 21);
    return tsp;
}

static u_char *
pcr_packet(int pid)
{
    u_char *pcr = malloc(TS_PACKET_SIZE);
    u_char *p = pcr;

    /* Transport packet header */
    p[0] = 0x47;
    st_unaligned16(htob_16(pid), p + 1);
    p[3] = 0x20;
    p += 4;

    *p++ = TS_PACKET_SIZE - 5;
    *p++ = 0x10;

    memset(p, 0xff, TS_PACKET_SIZE - 6);

    return pcr;
}

static void
init_pmt(mpegts_mux_t *tsm, int pid, int pcr_pid)
{
    u_char *pmt = malloc(TS_PACKET_SIZE);
    tsm->pmt = pmt;
    memset(pmt, 0xff, TS_PACKET_SIZE);
    
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
    st_unaligned16(htob_16(pcr_pid | 0xe000), pmt + 8);
    pmt[10] = 0;
    pmt[11] = 0;
    tsm->pmap = pmt + 12;
}

static int
write_ts_packet(mpegts_mux_t *tsm, int str, u_char *data, size_t size, 
		int ustart, uint64_t pts, uint64_t dts)
{
    struct mpegts_output_stream *os = tsm->streams + str;
    int cc = (os->ccount++ & 0xf) | 0x10;
    int psize;
    int dsize;
    int pid = os->pid;
    u_char *out = tsm->outbuf + tsm->bpos;
    u_char pesh[64];
    int peshl = 0;
    int stuffing;

    if(ustart){
	int pesflags = 0, pessize;

	if(pts != -1){
	    pesflags |= PES_FLAG_PTS;
	    if(dts != -1)
		pesflags |= PES_FLAG_DTS;
	}

	pessize = os->stream_type == STREAM_TYPE_VIDEO? 0: size;
	peshl = write_pes_header(pesh, os->stream_id,
				 pessize, pesflags, pts / 300,
				 dts / 300);
    }

    dsize = min(size, 184 - peshl);
    psize = dsize + peshl;
    stuffing = 184 - psize;

    *out++ = 0x47;

    if(ustart)
	pid |= 0x4000;

    if(stuffing)
	cc |= 0x20;

    st_unaligned16(htob_16(pid), out);
    out += 2;
    *out++ = cc;

    if(stuffing){
	int afl;

	if(stuffing > 1)
	    afl = stuffing - 1;
	else
	    afl = 0;

	*out++ = afl;
	if(afl > 0){
	    *out++ = 0;		/* flags */
	    afl--;
	    memset(out, 0xff, afl);
	    out += afl;
	}
    }

    memcpy(out, pesh, peshl);
    out += peshl;
    memcpy(out, data, dsize);

    return dsize;
}

static int
next_stream(mpegts_mux_t *tsm)
{
    uint64_t ts = -1;
    int s = -1, i;

    for(i = 0; i < tsm->astreams; i++){
	if(tsm->streams[i].pid){
	    uint64_t sts = tsm->streams[i].sts + tsm->streams[i].offset;
	    if(sts < ts){
		ts = sts;
		s = i;
	    }
	}
    }
    return s;
}

static void
post_packet(mpegts_mux_t *tsm)
{
    tsm->bpos += TS_PACKET_SIZE;
    if(tsm->bpos == tsm->bsize){
	if(tsm->realtime){
	    if(!tsm->started){
		tsm->timer->reset(tsm->timer, tsm->pcr);
		tsm->started = 1;
	    }
	    tsm->timer->wait(tsm->timer, tsm->pcr, NULL);
	}
	tsm->out->write(tsm->outbuf, 1, tsm->bpos, tsm->out);
	tsm->bpos = 0;
    }
    if(tsm->pcr != -1)
	tsm->pcr += TS_PACKET_SIZE * 27000000LL * 8 / tsm->bitrate;
    tsm->bytes += TS_PACKET_SIZE;
}

extern int
mpegts_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    mpegts_mux_t *tsm = p->private;
    int64_t dts;
    int nst;

    if(!pk->data){
	tc2_print("MPEGTS", TC2_PRINT_DEBUG,
		  "stream %i end\n", pk->stream);
	tsm->streams[pk->stream].tailtime = -1LL;
	tcfree(pk);
    } else {
	struct mpegts_output_stream *os = tsm->streams + pk->stream;

	if(pk->flags & TCVP_PKT_FLAG_PTS){
	    uint64_t dts = pk->flags & TCVP_PKT_FLAG_DTS? pk->dts: pk->pts;

	    if(os->sts == -1){
		if(tsm->pcr == -1){
		    if(dts >= tsm->delay)
			tsm->pcr_offset = -tsm->delay;
		    else
			tsm->pcr_offset = -dts;
		    tsm->delay += tsm->pcr_offset;

		    tc2_print("MPEGTS", TC2_PRINT_DEBUG,
			      "pcr_offset %lli, delay %lli\n",
			      tsm->pcr_offset, tsm->delay);

		    tsm->pcr = dts + tsm->pcr_offset;
		    tsm->start_time = tsm->pcr;
		}

		os->sts = dts + tsm->pcr_offset;
		os->tailtime = os->sts;
		
		tc2_print("MPEGTS", TC2_PRINT_DEBUG, "[%i] start %lli\n",
			  pk->stream, os->sts);
	    }

	    dts += tsm->pcr_offset;

	    if(dts - os->tailtime >= tsm->rate_lookahead){
		os->bitrate =
		    os->rbytes * 8 * 27000000LL / (dts - os->tailtime);
		tc2_print("MPEGTS", TC2_PRINT_DEBUG+5,
			  "[%i] rate %i, rbytes=%i\n",
			  pk->stream, os->bitrate, os->rbytes);
		os->tailtime = dts;
		os->rbytes = 0;
	    }

	    os->headtime = dts;
	}

	if(os->sts != -1){
	    tclist_push(os->packets, pk);
	    os->rbytes += pk->sizes[0];
	    tc2_print("MPEGTS", TC2_PRINT_DEBUG+8, "[%i] input rbytes=%i\n",
		      pk->stream, os->rbytes);
	} else {
	    tcfree(pk);
	}
    }

    nst = next_stream(tsm);
    tc2_print("MPEGTS", TC2_PRINT_DEBUG+9, "nst=%i\n", nst);

    while(nst > -1 && tclist_items(tsm->streams[nst].packets) &&
	  tsm->streams[nst].sts < tsm->streams[nst].tailtime){
	struct mpegts_output_stream *os = tsm->streams + nst;
	uint64_t ppts = -1, pdts = -1;
	char *data;
	int size, psize;

	pk = tclist_shift(os->packets);

	if(pk->flags & TCVP_PKT_FLAG_PTS){
	    if(pk->flags & TCVP_PKT_FLAG_DTS)
		dts = pk->dts + tsm->pcr_offset;
	    else
		dts = pk->pts + tsm->pcr_offset;

	    if(dts - os->lpts > tsm->pts_interval || os->lpts == -1){
		if(pk->flags & TCVP_PKT_FLAG_PTS)
		    ppts = pk->pts + tsm->delay;
		if(pk->flags & TCVP_PKT_FLAG_DTS)
		    pdts = pk->dts + tsm->delay;
		os->lpts = dts;
	    }


	    if(os->dts != -1){
		int64_t ddts = dts - os->dts;
		int64_t d = dts - os->sts;
		tc2_print("MPEGTS", TC2_PRINT_DEBUG+3,
			  "[%i] rate=%7i ddts=%lli dpcr=%lli d=%7lli\n",
			  nst, os->bitrate, ddts,
			  dts + tsm->delay - os->sts, d);
		if(ddts > tsm->rate_lookahead){
		    os->dts = dts;
		    os->bytes = 0;
		}
	    } else {
		os->dts = dts;
	    }

	    pk->flags &= ~TCVP_PKT_FLAG_PTS;
	}

	data = pk->data[0];
	size = pk->sizes[0];

	if(tsm->pcr - tsm->last_psi > tsm->psi_interval ||
	   tsm->last_psi == -1){
	    memcpy(tsm->outbuf + tsm->bpos, tsm->pat, TS_PACKET_SIZE);
	    inc_cc(tsm->pat);
	    post_packet(tsm);
	    memcpy(tsm->outbuf + tsm->bpos, tsm->pmt, TS_PACKET_SIZE);
	    inc_cc(tsm->pmt);
	    post_packet(tsm);
	    tsm->last_psi = tsm->pcr;
	}

	if(tsm->pcr - tsm->last_pcr > tsm->pcr_int || tsm->last_pcr == -1){
	    put_pcr(tsm->pcr_packet + 6, tsm->pcr);
	    memcpy(tsm->outbuf + tsm->bpos, tsm->pcr_packet, TS_PACKET_SIZE);
	    if(tsm->discont){
		*(tsm->outbuf + tsm->bpos + 5) |= 0x80;
		tsm->discont = 0;
	    }
	    inc_cc(tsm->pcr_packet);
	    tsm->last_pcr = tsm->pcr;
	    post_packet(tsm);
	}

	psize = write_ts_packet(tsm, pk->stream, data, size, os->unit_start,
				ppts, pdts);
	os->unit_start = 0;
	data += psize;
	size -= psize;
	if(os->dts != -1){
/* 	    os->sts += 27000000LL * 8 * psize / os->bitrate; */
	    os->bytes += psize;
	    os->sts = os->dts + 27000000LL * 8 * os->bytes / os->bitrate;
	}

	post_packet(tsm);

	if(tsm->bytes > TS_PACKET_SIZE * 10){
	    int64_t rate = tsm->bytes * 8 * 27000000LL / os->sts;
	    if(rate > tsm->bitrate)
		tsm->bitrate *= 1.000001;
	    while((int64_t) (os->sts - tsm->pcr) >
		  TS_PACKET_SIZE * 8 * 27000000LL / tsm->bitrate){
		memcpy(tsm->outbuf + tsm->bpos, tsm->null, TS_PACKET_SIZE);
		post_packet(tsm);
	    }
	}

	if(!size){
	    tcfree(pk);
	    os->unit_start = 1;
	    if(os->tailtime == -1LL && !tclist_items(os->packets)){
		os->sts = -1LL;
		os->pid = 0;
	    }
	} else {
	    pk->data[0] = data;
	    pk->sizes[0] = size;
	    tclist_unshift(os->packets, pk);
	}

	nst = next_stream(tsm);
    }

    return 0;
}

extern int
mpegts_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    mpegts_mux_t *tsm = p->private;
    mpeg_stream_type_t *str_type = mpeg_stream_type(s->common.codec);
    struct mpegts_output_stream *os;
    int pid;
    uint32_t crc;
    int esil = 0;
    u_char *ilp;
    int rate;

    if(!str_type){
	tc2_print("MPEGTS", TC2_PRINT_ERROR, "unsupported codec %s\n",
		  s->common.codec);
	return PROBE_FAIL;
    }

    pid = tsm->nextpid++;

    tc2_print("MPEGTS", TC2_PRINT_DEBUG,
	      "new stream %i, pid %x, type %x, rate %i\n",
	      s->common.index, pid, str_type->mpeg_stream_type,
	      s->common.bit_rate);

    *tsm->pmap++ = str_type->mpeg_stream_type;
    st_unaligned16(htob_16(pid | 0xe000), tsm->pmap);
    tsm->pmap += 2;
    ilp = tsm->pmap;
    tsm->pmap += 2;
    *tsm->pmt_slen += 5;

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

    os = tsm->streams + s->common.index;

    os->stream_type = s->stream_type;
    os->pid = pid;
    os->stream_id = str_type->stream_id_base;
/*     os->bitrate = s->common.bit_rate; */
    os->lpts = -1LL;
    os->dts = -1LL;
    os->sts = -1LL;
    os->packets = tclist_new(TC_LOCK_NONE);
    os->unit_start = 1;
    os->tailtime = -1LL;
    os->headtime = -1LL;

    if(os->stream_type == STREAM_TYPE_AUDIO)
	os->offset = tsm->audio_offset;
    else
	os->offset = tsm->video_offset;

    rate = s->common.bit_rate;
    if(!rate){
	if(s->stream_type == STREAM_TYPE_VIDEO)
	    rate = tsm->video_rate;
	else
	    rate = tsm->audio_rate;

	tc2_print("MPEGTS", TC2_PRINT_WARNING,
		  "bitrate not specified for stream %i, using default %i\n",
		  s->common.index, rate);
    }

    tsm->bitrate += rate * tsm->padding;

    return PROBE_OK;
}

extern int
mpegts_flush(tcvp_pipe_t *p, int drop)
{
    mpegts_mux_t *tsm = p->private;

    if(drop){
	tsm->bpos = 0;
    } else {
	if(tsm->bpos){
	    tsm->out->write(tsm->outbuf, 1, tsm->bpos, tsm->out);
	    tsm->bpos = 0;
	}
    }

    return 0;
}

static void
tmx_free(void *p)
{
    mpegts_mux_t *tsm = p;
    int i;

    tsm->out->close(tsm->out);
    free(tsm->outbuf);
    free(tsm->pat);
    free(tsm->pmt);
    free(tsm->pcr_packet);
    free(tsm->null);

    for(i = 0; i < tsm->astreams; i++)
	tclist_destroy(tsm->streams[i].packets, tcfree);

    free(tsm->streams);
    tcfree(tsm->timer);
}

extern int
mpegts_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	   muxed_stream_t *ms)
{
    mpegts_mux_t *tsm;
    char *url;
    url_t *out;

    if(tcconf_getvalue(cs, "mux/url", "%s", &url) <= 0){
	tc2_print("MPEGTS-MUX", TC2_PRINT_ERROR, "No output specified.\n");
	return -1;
    }

    if(!(out = url_open(url, "w"))){
	tc2_print("MPEGTS-MUX", TC2_PRINT_ERROR, "Error opening %s.\n", url);
	return -1;
    }

    tsm = tcallocdz(sizeof(*tsm), NULL, tmx_free);
    tsm->out = out;
    tsm->bsize = outbuf_size;

    tsm->nextpid = tcvp_demux_mpeg_conf_ts_start_pid;
    tsm->psi_interval = 1000;
    tsm->timer = tcref(t);
    tsm->pcr_int = mux_mpeg_ts_conf_pcr_interval;
    tsm->delay = tcvp_demux_mpeg_conf_ts_pcr_delay;
    tsm->discont = 1;

    tsm->start_time = -1;
    tsm->realtime = out->flags & URL_FLAG_STREAMED;
    tsm->pts_interval = tcvp_demux_mpeg_conf_pts_interval;
    tsm->rate_lookahead = tcvp_demux_mpeg_conf_ts_rate_lookahead;
    tsm->padding = 1.05;
    tsm->audio_rate = tcvp_demux_mpeg_conf_default_audio_rate;
    tsm->video_rate = tcvp_demux_mpeg_conf_default_video_rate;

    tcconf_getvalue(cs, "realtime", "%i", &tsm->realtime);
    tcconf_getvalue(cs, "delay", "%li", &tsm->delay);
    tcconf_getvalue(cs, "pts_interval", "%li", &tsm->pts_interval);
    tcconf_getvalue(cs, "psi_interval", "%i", &tsm->psi_interval);
    tcconf_getvalue(cs, "pcr_interval", "%i", &tsm->pcr_int);
    tcconf_getvalue(cs, "start_pid", "%i", &tsm->nextpid);
    tcconf_getvalue(cs, "rate_lookahead", "%li", &tsm->rate_lookahead);
    tcconf_getvalue(cs, "audio_offset", "%li", &tsm->audio_offset);
    tcconf_getvalue(cs, "video_offset", "%li", &tsm->video_offset);
    tcconf_getvalue(cs, "padding", "%lf", &tsm->padding);
    tcconf_getvalue(cs, "default_audio_rate", "%i", &tsm->audio_rate);
    tcconf_getvalue(cs, "default_video_rate", "%i", &tsm->video_rate);

    tsm->bitrate = 8 * TS_PACKET_SIZE * 1000 / tsm->pcr_int +
	2 * 8 * TS_PACKET_SIZE * 1000 / tsm->psi_interval;

    tsm->null = null_packet();
    tsm->pat = pat_packet(1, tsm->nextpid);
    tsm->pcr_pid = tsm->nextpid + 1;
    tsm->pcr_packet = pcr_packet(tsm->pcr_pid);
    init_pmt(tsm, tsm->nextpid++, tsm->pcr_pid);
    tsm->nextpid++;

    tsm->outbuf = malloc(tsm->bsize);
    tsm->pts_interval *= 27000;
    tsm->psi_interval *= 27000;
    tsm->pcr_int *= 27000;
    tsm->pcr = -1;
    tsm->last_pcr = -1;
    tsm->last_psi = -1;
    tsm->delay *= 27000;
    tsm->rate_lookahead *= 27000;
    tsm->audio_offset *= 27000;
    tsm->video_offset *= 27000;

    p->format.stream_type = STREAM_TYPE_MULTIPLEX;
    p->format.common.codec = "mpeg-ts";
    p->private = tsm;

    free(url);

    return 0;
}
