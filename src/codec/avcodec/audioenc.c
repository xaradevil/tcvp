/**
    Copyright (C) 2004-2005  Michael Ahlberg, Måns Rullgård

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

#define ENCBUFSIZE 4000

typedef struct avc_audioenc {
    char *codec;
    AVCodec *avc;
    AVCodecContext *ctx;
    uint8_t *outbuf;
    int bufsize;
    int bufpos;
    int eframesize;
    int bframes;
    uint8_t *inbuf;
    int framesize;
    int inbufpos;
    uint64_t pts;
    uint64_t ftime;
    int samplesize;
} avc_audioenc_t;

typedef struct avc_encpacket {
    tcvp_data_packet_t pk;
    u_char *data, *buf;
    int size;
} avc_encpacket_t;

#define min(a,b) ((a)<(b)?(a):(b))

static void
avc_free_pk(void *p)
{
    avc_encpacket_t *pk = p;
    free(pk->buf);
}

static int
encframe(tcvp_pipe_t *p, u_char *frame, int idx)
{
    avc_audioenc_t *enc = p->private;
    avc_encpacket_t *ep;
    int size, bsize;

    bsize = enc->bufsize - enc->bufpos;
    size = avcodec_encode_audio(enc->ctx, enc->outbuf + enc->bufpos,
				bsize, (int16_t *) frame);
    enc->bufpos += size;
    enc->bframes++;

    if(enc->bufsize - enc->bufpos < enc->eframesize){
	ep = tcallocdz(sizeof(*ep), NULL, avc_free_pk);
	ep->pk.stream = idx;
	ep->pk.data = &ep->data;
	ep->pk.sizes = &ep->size;
	ep->pk.planes = 1;
	ep->pk.flags = 0;
	if(enc->pts != -1LL){
	    ep->pk.flags |= TCVP_PKT_FLAG_PTS;
	    ep->pk.pts = enc->pts;
	    enc->pts = -1LL;
	}
	ep->buf = ep->data = enc->outbuf;
	ep->size = enc->bufpos;

	enc->outbuf = malloc(enc->bufsize);
	enc->bufpos = 0;
	enc->bframes = 0;

	p->next->input(p->next, (tcvp_packet_t *) ep);
    }

    return size;
}

extern int
avc_audioenc(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    avc_audioenc_t *enc = p->private;
    int size;
    u_char *data;

    if(!pk->data)
	return p->next->input(p->next, (tcvp_packet_t *) pk);

    data = pk->data[0];
    size = pk->sizes[0];

    if(pk->flags & TCVP_PKT_FLAG_PTS && enc->pts == -1LL){
	enc->pts = pk->pts - 27000000LL * enc->inbufpos /
	    (enc->ctx->sample_rate * enc->samplesize) -
	    enc->bframes * enc->ftime;
    }

    if(enc->inbufpos){
	int rs = min(enc->framesize - enc->inbufpos, size);

	memcpy(enc->inbuf + enc->inbufpos, data, rs);
	enc->inbufpos += rs;
	data += rs;
	size -= rs;

	if(enc->inbufpos == enc->framesize){
	    encframe(p, enc->inbuf, pk->stream);
	    enc->inbufpos = 0;
	}
    }

    while(size > enc->framesize){
	encframe(p, data, pk->stream);
	data += enc->framesize;
	size -= enc->framesize;
    }

    if(size > 0){
	memcpy(enc->inbuf, data, size);
	enc->inbufpos = size;
    }

    tcfree(pk);

    return 0;
}

extern int
avc_audioenc_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    avc_audioenc_t *enc = p->private;
    AVCodecContext *ctx = enc->ctx;

    if(s->stream_type != STREAM_TYPE_AUDIO)
	return PROBE_FAIL;

    p->format.common.codec = enc->codec;
    p->format.common.bit_rate = ctx->bit_rate;

    ctx->sample_rate = s->audio.sample_rate;
    ctx->channels = s->audio.channels;
    ctx->sample_fmt = SAMPLE_FMT_S16;

    if(avcodec_open(ctx, enc->avc) < 0){
	ctx->codec = NULL;
	return PROBE_FAIL;
    }

    enc->samplesize = ctx->channels * 2;
    enc->ftime = 27000000LL * enc->framesize / ctx->sample_rate;
    enc->framesize *= enc->samplesize;

    enc->inbuf = malloc(enc->framesize);
    enc->outbuf = malloc(enc->bufsize);

    return PROBE_OK;
}

extern int
avc_audioenc_flush(tcvp_pipe_t *p, int drop)
{
    avc_audioenc_t *enc = p->private;

    if(drop && enc->ctx->codec)
	avcodec_flush_buffers(enc->ctx);

    return 0;
}

static void
avc_free_audioenc(void *p)
{
    avc_audioenc_t *enc = p;

    free(enc->inbuf);
    free(enc->outbuf);
    if(enc->ctx->codec)
	avcodec_close(enc->ctx);
}

extern int
avc_audioenc_new(tcvp_pipe_t *p, stream_t *s, char *codec,
		 tcconf_section_t *cf)
{
    AVCodec *avc;
    AVCodecContext *ctx;
    avc_audioenc_t *enc;
    char *avcname;

    avcname = avc_codec_name(codec);
    avc = avcodec_find_encoder_by_name(avcname);
    free(avcname);

    if(!avc){
	tc2_print("AVCODEC", TC2_PRINT_ERROR,
		  "Can't find encoder for '%s'.\n", codec);
	return -1;
    }

    ctx = avcodec_alloc_context();
    avcodec_get_context_defaults(ctx);

#define ctx_conf(n, f) tcconf_getvalue(cf, #n, "%"#f, &ctx->n)
    ctx_conf(bit_rate, i);
    ctx_conf(flags, i);
    ctx_conf(debug, i);

    enc = tcallocdz(sizeof(*enc), NULL, avc_free_audioenc);
    enc->codec = codec;
    enc->avc = avc;
    enc->ctx = ctx;
    enc->bufsize = ENCBUFSIZE;

    if(avc->id == CODEC_ID_MP2){
	enc->eframesize = 1792;
	enc->framesize = 1152;
    } else if(avc->id == CODEC_ID_AC3){
	enc->eframesize = 3840;
	enc->framesize = 1536;
    }

    enc->pts = -1LL;

    p->format = *s;
    p->format.common.codec = codec;
    p->private = enc;

    return 0;
}

#define avc_enc_new(cd)							\
extern int								\
avc_##cd##_enc_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,	\
		   tcvp_timer_t *t, muxed_stream_t *ms)			\
{									\
    return avc_audioenc_new(p, s, "audio/"#cd, cs);			\
}

avc_enc_new(mp2)
avc_enc_new(ac3)
