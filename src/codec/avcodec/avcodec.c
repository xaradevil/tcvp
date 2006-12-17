/**
    Copyright (C) 2003-2006  Michael Ahlberg, Måns Rullgård

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
#include <tcvp_types.h>
#include <ffmpeg/avcodec.h>
#include <avcodec_tc2.h>
#include "avc.h"

static pthread_mutex_t avc_lock = PTHREAD_MUTEX_INITIALIZER;

extern int
avc_init(char *arg)
{
    avcodec_init();
    avcodec_register_all();
    return 0;
}

extern void
avc_free_packet(void *v)
{
    tcvp_data_packet_t *p = v;
    free(p->sizes);
}

extern void
avc_free_pipe(void *p)
{
    avc_codec_t *vc = p;

    pthread_mutex_lock(&avc_lock);
    avcodec_close(vc->ctx);
    pthread_mutex_unlock(&avc_lock);

    av_free(vc->ctx);
    if(vc->pctx)
        av_parser_close(vc->pctx);
    free(vc->buf);
    free(vc->frame);
}

extern int
avc_flush(tcvp_pipe_t *p, int drop)
{
    avc_codec_t *ac = p->private;

    if(drop && ac->ctx->codec)
	avcodec_flush_buffers(ac->ctx);

    return 0;
}

extern int
avc_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
	tcvp_timer_t *t, muxed_stream_t *ms)
{
    avc_codec_t *ac;
    AVCodec *avc = NULL;
    AVCodecContext *avctx;
    AVCodecParserContext *pctx = NULL;
    char *avcname;

    avcname = avc_codec_name(s->common.codec);
    if(!avcname)
	return -1;

    avc = avcodec_find_decoder_by_name(avcname);
    free(avcname);

    if(!avc){
	tc2_print("AVCODEC", TC2_PRINT_ERROR,
		  "Can't find codec for '%s'\n", s->common.codec);
	return -1;
    }

    avctx = avcodec_alloc_context();

    if(s->common.flags & TCVP_STREAM_FLAG_TRUNCATED){
        pctx = av_parser_init(avc->id);
    }

#define ctx_conf(n, f) tcconf_getvalue(cs, #n, "%"#f, &avctx->n)
    ctx_conf(workaround_bugs, i);
    ctx_conf(error_resilience, i);
    ctx_conf(error_concealment, i);
    ctx_conf(idct_algo, i);
    ctx_conf(debug, i);

    ac = tcallocdz(sizeof(*ac), NULL, avc_free_pipe);
    ac->ctx = avctx;
    ac->pctx = pctx;
    p->private = ac;

    switch(s->stream_type){
    case STREAM_TYPE_AUDIO:
	avctx->sample_rate = s->audio.sample_rate;
	avctx->channels = s->audio.channels;
	avctx->bit_rate = s->audio.bit_rate;
	avctx->block_align = s->audio.block_align;
	ac->buf = malloc(10 * 1048576);
	p->format.common.codec = "audio/pcm-s16" TCVP_ENDIAN;
	break;

    case STREAM_TYPE_VIDEO:
	avctx->width = s->video.width;
	avctx->height = s->video.height;
	avctx->time_base.den = s->video.frame_rate.num;
	avctx->time_base.num = s->video.frame_rate.den;
	ac->ptsn = (uint64_t) 27000000 * s->video.frame_rate.den;
	ac->ptsd = s->video.frame_rate.num?: 1;
	ac->pts = 0;
	ac->frame = avcodec_alloc_frame();
	memset(ac->ptsq, 0xff, sizeof(ac->ptsq));

	p->format.common.codec = "video/raw-i420";
	break;

    default:
	tc2_print("AVCODEC", TC2_PRINT_ERROR,
		  "unknown stream type %i\n", s->stream_type);
	return -1;
    }

    avctx->extradata = s->common.codec_data;
    avctx->extradata_size = s->common.codec_data_size;

    pthread_mutex_lock(&avc_lock);
    avcodec_open(avctx, avc);
    pthread_mutex_unlock(&avc_lock);

    return 0;
}

static char *codec_names[][2] = {
    { "video/mpeg", "mpeg1video" },
    { "video/mpeg2", "mpeg2video" },
    { "video/dv", "dvvideo" },
    { "video/msmpeg4v3", "msmpeg4" },
    { "audio/mpeg", "mp2" },
    { }
};

extern char *
avc_codec_name(char *codec)
{
    char *cn;
    int i;

    for(i = 0; codec_names[i][0]; i++){
	if(!strcmp(codec, codec_names[i][0])){
	    return strdup(codec_names[i][1]);
	}
    }

    cn = strchr(codec, '/');
    if(!cn)
	return NULL;

    cn = strdup(cn + 1);

    for(i = 0; cn[i]; i++)
	if(cn[i] == '-')
	    cn[i] = '_';

    return cn;
}

static char *
avc_codec_rname(const char *name)
{
    char *cn;
    int i;

    for(i = 0; codec_names[i][0]; i++){
	if(!strcmp(name, codec_names[i][1])){
	    cn = strchr(codec_names[i][0], '/');
	    return strdup(cn + 1);
	}
    }

    cn = strdup(name);
    for(i = 0; cn[i]; i++)
	if(cn[i] == '_')
	    cn[i] = '-';

    return cn;
}

static void
add_codec(const char *name, char *type, char *dec, char *enc)
{
    char buf[512];
    if(dec){
	snprintf(buf, sizeof(buf), "decoder/%s/%s", type, name);
	tc2_add_export(THIS_MODULE, buf, "new", dec);
    }
    if(enc){
	snprintf(buf, sizeof(buf), "encoder/%s/%s", type, name);
	tc2_add_export(THIS_MODULE, buf, "new", enc);
    }
}

extern int
avc_setup(void)
{
    AVCodec *avc;
    avcodec_register_all();

    for(avc = first_avcodec; avc; avc = avc->next){
	char *type, *enc, *dec, *name;
	if(avc->type == CODEC_TYPE_VIDEO){
	    type = "video";
	    dec = "_tcvp_decoder_video_mpeg_new";
	    enc = "_tcvp_encoder_video_mpeg_new";
	} else if(avc->type == CODEC_TYPE_AUDIO){
	    type = "audio";
	    dec = "_tcvp_decoder_audio_mpeg_new";
	    enc = "_tcvp_encoder_audio_mpeg_new";
	} else {
	    continue;
	}
	name = avc_codec_rname(avc->name);
	if(!avc->decode)
	    dec = NULL;
	if(!avc->encode)
	    enc = NULL;
	add_codec(name? name: avc->name, type, dec, enc);
	free(name);
    }

    return 0;
}
