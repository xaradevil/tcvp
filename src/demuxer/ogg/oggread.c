/**
    Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

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
#include <tcalloc.h>
#include <tcendian.h>
#include "ogg.h"

#define MAX_PAGE_SIZE 65307
#define BUFSIZE MAX_PAGE_SIZE

typedef struct {
    tcvp_data_packet_t pk;
    u_char *data, *buf;
    int size;
} ogg_data_packet_t;

static ogg_codec_t *ogg_codecs[];

static int
ogg_save(ogg_t *ogg)
{
    ogg_state_t *ost =
	malloc(sizeof(*ost) + ogg->nstreams * sizeof(*ogg->streams));
    int i;

    ost->pos = ogg->f->tell(ogg->f);
    ost->curidx = ogg->curidx;
    ost->next = ogg->state;
    memcpy(ost->streams, ogg->streams, ogg->nstreams * sizeof(*ogg->streams));

    for(i = 0; i < ogg->nstreams; i++){
	ogg_stream_t *os = ogg->streams + i;
	os->buf = tcalloc(os->bufsize);
	memcpy(os->buf, ost->streams[i].buf, os->bufpos);
    }

    ogg->state = ost;

    return 0;
}

static int
ogg_restore(ogg_t *ogg, int discard)
{
    ogg_state_t *ost = ogg->state;
    int i;

    if(!ost)
	return 0;

    ogg->state = ost->next;

    if(!discard){
	for(i = 0; i < ogg->nstreams; i++)
	    tcfree(ogg->streams[i].buf);

	ogg->f->seek(ogg->f, ost->pos, SEEK_SET);
	ogg->curidx = ost->curidx;
	memcpy(ogg->streams, ost->streams,
	       ogg->nstreams * sizeof(*ogg->streams));
    }

    free(ost);

    return 0;
}

static int
ogg_new_stream(muxed_stream_t *ms, uint32_t serial)
{
    ogg_t *ogg = ms->private;
    int idx = ogg->nstreams++;
    ogg_stream_t *os;

    ogg->streams = realloc(ogg->streams, ogg->nstreams*sizeof(*ogg->streams));
    memset(ogg->streams + idx, 0, sizeof(*ogg->streams));
    os = ogg->streams + idx;
    os->serial = serial;
    os->bufsize = BUFSIZE;
    os->buf = tcalloc(os->bufsize);
    os->header = -1;

    ms->n_streams = ogg->nstreams;
    ms->streams = realloc(ms->streams, ogg->nstreams * sizeof(*ms->streams));
    memset(ms->streams + idx, 0, sizeof(*ms->streams));

    return idx;
}

static uint64_t
ogg_gptopts(muxed_stream_t *ms, int i, uint64_t gp)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os = ogg->streams + i;
    stream_t *st = ms->streams + i;
    uint64_t pts = -1;

    if(os->codec->gptopts){
	pts = os->codec->gptopts(ms, i, gp);
    } else if(st->stream_type == STREAM_TYPE_AUDIO){
	pts = gp * 27000000LL / st->audio.sample_rate;
    } else if(st->stream_type == STREAM_TYPE_VIDEO){
	pts = gp * st->video.frame_rate.den * 27000000LL /
	    st->video.frame_rate.num;
    }

    return pts;
}

static ogg_codec_t *
ogg_find_codec(u_char *buf, int size)
{
    int i;

    for(i = 0; ogg_codecs[i]; i++)
	if(size >= ogg_codecs[i]->magicsize &&
	   !memcmp(buf, ogg_codecs[i]->magic, ogg_codecs[i]->magicsize))
	    return ogg_codecs[i];

    return NULL;
}

static int
ogg_find_stream(ogg_t *ogg, int serial)
{
    int i;

    for(i = 0; i < ogg->nstreams; i++)
	if(ogg->streams[i].serial == serial)
	    return i;

    return -1;
}

static int
ogg_read_page(muxed_stream_t *ms, int *str)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os;
    int i = 0;
    int flags, nsegs;
    uint64_t gp;
    uint32_t serial;
    uint32_t seq;
    uint32_t crc;
    int size, idx;
    char sync[4];
    int sp = 0;

    if(ogg->f->read(sync, 1, 4, ogg->f) < 4)
	return -1;

    do {
	int c;

	if(sync[sp & 3] == 'O' &&
	   sync[(sp+1) & 3] == 'g' &&
	   sync[(sp+2) & 3] == 'g' &&
	   sync[(sp+3) & 3] == 'S')
	    break;

	c = url_getc(ogg->f);
	if(c < 0)
	    return -1;
	sync[sp++ & 3] = c;
    } while(i++ < MAX_PAGE_SIZE);

    if(i >= MAX_PAGE_SIZE){
	tc2_print("OGG", TC2_PRINT_WARNING, "can't find sync word\n");
	return -1;
    }

    if(url_getc(ogg->f) != 0)	/* version */
	return -1;

    flags = url_getc(ogg->f);
    url_getu64l(ogg->f, &gp);
    url_getu32l(ogg->f, &serial);
    url_getu32l(ogg->f, &seq);
    url_getu32l(ogg->f, &crc);
    nsegs = url_getc(ogg->f);

    idx = ogg_find_stream(ogg, serial);
    if(idx < 0){
	idx = ogg_new_stream(ms, serial);
	if(idx < 0)
	    return -1;
    }

    os = ogg->streams + idx;

    if(ogg->f->read(os->segments, 1, nsegs, ogg->f) < nsegs)
	return -1;

    os->nsegs = nsegs;
    os->segp = 0;

    size = 0;
    for(i = 0; i < nsegs; i++)
	size += os->segments[i];

    if(flags & OGG_FLAG_CONT){
	if(!os->psize){
	    while(os->segp < os->nsegs){
		int s = os->segments[os->segp++];
		os->pstart += s;
		if(s < 255)
		    break;
	    }
	}
    } else {
	os->psize = 0;
    }

    if(os->bufsize - os->bufpos < size){
	u_char *nb = tcalloc(os->bufsize *= 2);
	memcpy(nb, os->buf, os->bufpos);
	tcfree(os->buf);
	os->buf = nb;
    }

    if(ogg->f->read(os->buf + os->bufpos, 1, size, ogg->f) < size)
	return -1;

    os->lastgp = os->granule;
    os->bufpos += size;
    os->granule = gp;
    os->flags = flags;

    if(str && os->header > -2)
	*str = idx;

    return 0;
}

static int
ogg_packet(muxed_stream_t *ms, int *str)
{
    ogg_t *ogg = ms->private;
    int idx;
    ogg_stream_t *os;
    int complete = 0;
    int segp = 0, psize = 0;

    tc2_print("OGG", TC2_PRINT_DEBUG+3, "ogg_packet: curidx=%i\n",
	      ogg->curidx);

    do {
	idx = ogg->curidx;

	while(idx < 0){
	    if(ogg_read_page(ms, &idx) < 0)
		return -1;
	}

	os = ogg->streams + idx;

	tc2_print("OGG", TC2_PRINT_DEBUG+2,
		  "ogg_packet: idx=%i pstart=%i psize=%i segp=%i nsegs=%i\n",
		  idx, os->pstart, os->psize, os->segp, os->nsegs);

	if(!os->codec){
	    if(os->header == -1){
		os->codec = ogg_find_codec(os->buf, os->bufpos);
		if(!os->codec){
		    os->header = -2;
		    return 0;
		}
	    } else {
		return 0;
	    }
	}

	segp = os->segp;
	psize = os->psize;

	while(os->segp < os->nsegs){
	    int ss = os->segments[os->segp++];
	    os->psize += ss;
	    if(ss < 255){
		complete = 1;
		break;
	    }
	}

	if(!complete && os->segp == os->nsegs){
	    u_char *nb = tcalloc(os->bufsize);
	    int size = os->bufpos - os->pstart;
	    memcpy(nb, os->buf + os->pstart, size);
	    tcfree(os->buf);
	    os->buf = nb;
	    os->bufpos = size;
	    os->pstart = 0;
	    ogg->curidx = -1;
	}
    } while(!complete);

    tc2_print("OGG", TC2_PRINT_DEBUG+2,
	      "ogg_packet: idx %i, frame size %i, start %i\n",
	      idx, os->psize, os->pstart);

    ogg->curidx = idx;

    if(os->header == -1){
	int hdr = os->codec->header(ms, idx);
	if(!hdr){
	    os->header = os->seq;
	    os->segp = segp;
	    os->psize = psize;
	    ogg->headers = 1;
	} else {
	    if(hdr < 0)
		os->header = -2;
	    os->pstart += os->psize;
	    os->psize = 0;
	}
    }

    if(os->header > -1 && os->seq > os->header){
	if(os->codec && os->codec->packet)
	    os->codec->packet(ms, idx);
	if(str)
	    *str = idx;
    }

    os->seq++;
    if(os->segp == os->nsegs)
	ogg->curidx = -1;

    return 0;
}

static void
ogg_free_packet(void *v)
{
    ogg_data_packet_t *p = v;
    tcfree(p->buf);
}

extern tcvp_packet_t *
ogg_next_packet(muxed_stream_t *ms, int stream)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os;
    ogg_data_packet_t *pk;
    int idx = -1;

    do {
	if(ogg_packet(ms, &idx) < 0)
	    return NULL;
    } while(idx < 0 || !ms->used_streams[idx]);

    os = ogg->streams + idx;

    pk = tcallocdz(sizeof(*pk), NULL, ogg_free_packet);
    pk->pk.stream = idx;
    pk->pk.data = &pk->data;
    pk->pk.sizes = &pk->size;
    pk->pk.planes = 1;
    pk->size = os->psize;
    pk->data = os->buf + os->pstart;
    pk->buf = tcref(os->buf);

    if(os->lastgp != -1LL){
	pk->pk.flags |= TCVP_PKT_FLAG_PTS;
	pk->pk.pts = ogg_gptopts(ms, idx, os->lastgp);
	os->lastgp = -1;
    }

    os->pstart += os->psize;
    os->psize = 0;

    return (tcvp_packet_t *) pk;
}

static int
ogg_reset(ogg_t *ogg)
{
    int i;

    for(i = 0; i < ogg->nstreams; i++){
	ogg_stream_t *os = ogg->streams + i;
	os->bufpos = 0;
	os->pstart = 0;
	os->psize = 0;
	os->granule = -1;
	os->lastgp = -1;
	os->nsegs = 0;
	os->segp = 0;
    }

    ogg->curidx = -1;

    return 0;
}

#define absdiff(a, b) ((a)>(b)? (a)-(b): (b)-(a))

static uint64_t
ogg_seek(muxed_stream_t *ms, uint64_t time)
{
    ogg_t *ogg = ms->private;
    uint64_t min = 0, max = ogg->size;
    uint64_t tmin = 0, tmax = ms->time;
    uint64_t pts = -1;

    ogg_save(ogg);

    while(min <= max){
	uint64_t p = min + (max - min) * (time - tmin) / (tmax - tmin);
	int i = -1;

	ogg->f->seek(ogg->f, p, SEEK_SET);

	while(!ogg_read_page(ms, &i)){
	    if(i >= 0 && ogg->streams[i].granule != 0 &&
	       ogg->streams[i].granule != -1)
		break;
	}

	if(i == -1)
	    break;

	pts = ogg_gptopts(ms, i, ogg->streams[i].granule);
	p = ogg->f->tell(ogg->f);

	if(absdiff(pts, time) < 27000000LL)
	    break;

	if(pts > time){
	    max = p;
	    tmax = pts;
	} else {
	    min = p;
	    tmin = pts;
	}
    }

    if(absdiff(pts, time) < 27000000LL){
	ogg_restore(ogg, 1);
	ogg_reset(ogg);
    } else {
	ogg_restore(ogg, 0);
	pts = -1;
    }

    return pts;
}

static int
ogg_get_headers(muxed_stream_t *ms)
{
    ogg_t *ogg = ms->private;

    do {
	if(ogg_packet(ms, NULL) < 0)
	    return -1;
    } while(!ogg->headers);

    tc2_print("OGG", TC2_PRINT_DEBUG, "found headers\n");

    return 0;
}

static int
ogg_get_length(muxed_stream_t *ms)
{
    ogg_t *ogg = ms->private;
    uint64_t end;
    int i;

    if(ogg->f->flags & URL_FLAG_STREAMED)
	return 0;
    if(ms->time)
	return 0;
    if(!ogg->f->seek)
	return 0;

    ogg_save(ogg);
    ogg->f->seek(ogg->f, -MAX_PAGE_SIZE, SEEK_END);

    while(!ogg_read_page(ms, &i)){
	if(i >= 0 && ogg->streams[i].granule != -1 &&
	   ogg->streams[i].granule != 0)
	    ms->time = ogg_gptopts(ms, i, ogg->streams[i].granule);
    }

    end = ogg->f->tell(ogg->f);
    ogg->size = end;

    ogg_restore(ogg, 0);

    return 0;
}

static void
ogg_free(void *p)
{
    muxed_stream_t *ms = p;
    ogg_t *ogg = ms->private;
    int i;

    for(i = 0; i < ogg->nstreams; i++){
	tcfree(ogg->streams[i].buf);
	tcfree(ogg->streams[i].private);
	free(ms->streams[i].common.codec_data);
    }

    free(ogg->streams);
    free(ogg);

    free(ms->streams);
    free(ms->used_streams);
}

extern muxed_stream_t *
ogg_open(char *name, url_t *f, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    ogg_t *ogg;
    muxed_stream_t *ms;

    ogg = calloc(1, sizeof(*ogg));
    ogg->f = tcref(f);
    ogg->curidx = -1;

    ms = tcallocdz(sizeof(*ms), NULL, ogg_free);
    ms->next_packet = ogg_next_packet;
    ms->seek = ogg_seek;
    ms->private = ogg;

    if(ogg_get_headers(ms) < 0){
	tcfree(ms);
	return NULL;
    }

    ogg_get_length(ms);

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));

    return ms;
}

static ogg_codec_t *ogg_codecs[] = {
    &vorbis_codec,
    &flac_codec,
    &theora_codec,
    &ogm_video_codec,
    &ogm_audio_codec,
    &ogm_old_codec,
    NULL
};
