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

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <pthread.h>
#include <ffmpeg/avformat.h>
#include <avf.h>
#include <avformat_tc2.h>

typedef struct avf_write {
    AVFormatContext fc;
    int header;
    int nstreams, astreams;
    int mapsize;
    struct {
	int avidx;
	uint64_t dts;
	int used;
    } *streams;
    pthread_mutex_t lock;
    pthread_cond_t cnd;
} avf_write_t;

static int
avfw_input(tcvp_pipe_t *p, packet_t *pk)
{
    avf_write_t *avf = p->private;
    int ai;

    pthread_mutex_lock(&avf->lock);

    if(pk->flags & TCVP_PKT_FLAG_DTS){
	avf->streams[pk->stream].dts = pk->dts;
    } else if(pk->flags & TCVP_PKT_FLAG_PTS){
	avf->streams[pk->stream].dts = pk->pts;
    }

    pthread_cond_broadcast(&avf->cnd);

    if(!pk->data){
	if(!--avf->nstreams)
	    av_write_trailer(&avf->fc);
	avf->streams[pk->stream].used = 0;
	pthread_cond_broadcast(&avf->cnd);
	pthread_mutex_unlock(&avf->lock);
	goto out;
    }

    if(!avf->header){
	av_write_header(&avf->fc);
	avf->header = 1;
    }
    pthread_mutex_unlock(&avf->lock);

    ai = avf->streams[pk->stream].avidx;
    avf->fc.streams[ai]->codec.coded_frame->key_frame =
	pk->flags & TCVP_PKT_FLAG_KEY;
    av_write_frame(&avf->fc, ai, pk->data[0], pk->sizes[0]);

out:
    tcfree(pk);
    return 0;
}

static int
avfw_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    avf_write_t *avf = p->private;
    AVStream *as;
    int ai;

    if(avf->astreams <= s->common.index){
	int ns = s->common.index + 1;
	avf->streams = realloc(avf->streams, ns * sizeof(*avf->streams));
	memset(avf->streams + avf->astreams, 0,
	       (ns - avf->astreams) * sizeof(*avf->streams));
	avf->astreams = ns;
    }

    av_new_stream(&avf->fc, s->common.index);
    ai = avf->nstreams++;
    avf->streams[s->common.index].avidx = ai;
    avf->streams[s->common.index].used = 1;
    as = avf->fc.streams[ai];
    as->codec.codec_id = avf_codec_id(s->common.codec);
    as->codec.coded_frame = avcodec_alloc_frame();
    as->codec.bit_rate = s->common.bit_rate;
    if(s->stream_type == STREAM_TYPE_VIDEO){
	as->codec.codec_type = CODEC_TYPE_VIDEO;
	as->codec.frame_rate = s->video.frame_rate.num;
	as->codec.frame_rate_base = s->video.frame_rate.den;
	as->codec.width = s->video.width;
	as->codec.height = s->video.height;
    } else if(s->stream_type == STREAM_TYPE_AUDIO){
	as->codec.codec_type = CODEC_TYPE_AUDIO;
	as->codec.sample_rate = s->audio.sample_rate;
	as->codec.channels = s->audio.channels;
    }

    return PROBE_OK;
}

static int
avfw_flush(tcvp_pipe_t *p, int drop)
{
    return 0;
}

static void
avfw_free(void *p)
{
    tcvp_pipe_t *tp = p;
    avf_write_t *avf = tp->private;

    url_fclose(&avf->fc.pb);
    if(avf->streams)
	free(avf->streams);
    pthread_mutex_destroy(&avf->lock);
    pthread_cond_destroy(&avf->cnd);
}

extern tcvp_pipe_t *
avfw_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    tcvp_pipe_t *p;
    avf_write_t *avf;
    AVOutputFormat *of;
    char *ofn;

    if(tcconf_getvalue(cs, "mux/url", "%s", &ofn) <= 0)
	return NULL;

    if(!(of = guess_format(NULL, ofn, NULL)))
	return NULL;

    avf = calloc(1, sizeof(*avf));
    avf->fc.oformat = of;
    pthread_mutex_init(&avf->lock, NULL);
    pthread_cond_init(&avf->cnd, NULL);

    if(url_fopen(&avf->fc.pb, ofn, URL_WRONLY)){
	free(avf);
	return NULL;
    }
    av_set_parameters(&avf->fc, NULL);

    p = tcallocdz(sizeof(*p), NULL, avfw_free);
    p->input = avfw_input;
    p->probe = avfw_probe;
    p->flush = avfw_flush;
    p->private = avf;

    p->format.stream_type = STREAM_TYPE_MULTIPLEX;
    p->format.common.codec = of->name;

    free(ofn);
    return p;
}
