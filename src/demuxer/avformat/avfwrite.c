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
#include <tcalloc.h>
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
} avf_write_t;

extern int
avfw_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    avf_write_t *avf = p->private;
    int ai;

    if(!pk->data){
	if(!--avf->nstreams)
	    av_write_trailer(&avf->fc);
	avf->streams[pk->stream].used = 0;
	goto out;
    }

    if(!avf->header){
	av_write_header(&avf->fc);
	avf->header = 1;
    }

    ai = avf->streams[pk->stream].avidx;
#if LIBAVFORMAT_BUILD < 4615
    if(pk->flags & TCVP_PKT_FLAG_DTS){
	avf->streams[pk->stream].dts = pk->dts;
    } else if(pk->flags & TCVP_PKT_FLAG_PTS){
	avf->streams[pk->stream].dts = pk->pts;
    }
    avf->fc.streams[ai]->codec.coded_frame->key_frame =
	pk->flags & TCVP_PKT_FLAG_KEY;
    av_write_frame(&avf->fc, ai, pk->data[0], pk->sizes[0]);
#else
    {
	AVPacket ap;
	av_init_packet(&ap);
	if(pk->flags & TCVP_PKT_FLAG_PTS)
	    ap.pts = pk->pts * avf->fc.streams[ai]->time_base.den /
		(27000000 * avf->fc.streams[ai]->time_base.num);
	if(pk->flags & TCVP_PKT_FLAG_DTS)
	    ap.dts = pk->dts * avf->fc.streams[ai]->time_base.den /
		(27000000 * avf->fc.streams[ai]->time_base.num);
	ap.data = pk->data[0];
	ap.size = pk->sizes[0];
	ap.stream_index = ai;
	if(pk->flags & TCVP_PKT_FLAG_KEY)
	    ap.flags |= PKT_FLAG_KEY;
	ap.destruct = NULL;
	av_write_frame(&avf->fc, &ap);
    }
#endif

out:
    tcfree(pk);
    return 0;
}

extern int
avfw_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
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

    tcfree(pk);
    return PROBE_OK;
}

static void
avfw_free(void *p)
{
    avf_write_t *avf = p;
    int i;

    url_fclose(&avf->fc.pb);
    free(avf->streams);

    for(i = 0; i < avf->fc.nb_streams; i++){
	av_free(avf->fc.streams[i]->codec.coded_frame);
	av_free(avf->fc.streams[i]);
    }
}

extern int
avfw_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    avf_write_t *avf;
    AVOutputFormat *of;
    char *ofn;

    if(tcconf_getvalue(cs, "mux/url", "%s", &ofn) <= 0)
	return -1;

    if(!(of = guess_format(NULL, ofn, NULL)))
	return -1;

    avf = tcallocdz(sizeof(*avf), NULL, avfw_free);
    avf->fc.oformat = of;

    if(url_fopen(&avf->fc.pb, ofn, URL_WRONLY)){
	free(avf);
	return -1;
    }
    av_set_parameters(&avf->fc, NULL);

    p->private = avf;

    p->format.stream_type = STREAM_TYPE_MULTIPLEX;

    free(ofn);
    return 0;
}
