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
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <ffmpeg/avcodec.h>
#include <avcodec_tc2.h>
#include "avc.h"

#define ENCBUFSIZE 1048576

typedef struct avc_encvid {
    char *codec;
    AVCodec *avc;
    AVCodecContext *ctx;
    AVFrame *frame;
    uint8_t *buf;
} avc_encvid_t;

typedef struct avc_encpacket {
    packet_t pk;
    u_char *data;
    int size;
} avc_encpacket_t;

static void
avc_free_encpacket(packet_t *p)
{
    free(p);
}

static int
avc_encvid(tcvp_pipe_t *p, packet_t *pk)
{
    avc_encvid_t *enc = p->private;
    AVFrame *f = enc->frame;
    avc_encpacket_t *ep;
    int i, size;

    if(!pk->data)
	return p->next->input(p->next, pk);

    for(i = 0; i < 3; i++){
	f->data[i] = pk->data[i];
	f->linesize[i] = pk->sizes[i];
    }

    f->pts = pk->pts;

    if((size = avcodec_encode_video(enc->ctx, enc->buf, ENCBUFSIZE, f)) > 0){
/* 	fprintf(stderr, "%lli %lli\n", pk->pts, enc->ctx->coded_frame->pts); */
	ep = calloc(1, sizeof(*ep));
	ep->pk.stream = pk->stream;
	ep->pk.data = &ep->data;
	ep->pk.sizes = &ep->size;
	ep->pk.planes = 1;
	ep->pk.flags = TCVP_PKT_FLAG_PTS;
	ep->pk.pts = pk->pts;
	ep->pk.free = avc_free_encpacket;
	ep->data = enc->buf;
	ep->size = size;
	p->next->input(p->next, &ep->pk);
    }

    return 0;
}

static int
avc_encvideo_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    avc_encvid_t *enc = p->private;
    AVCodecContext *ctx = enc->ctx;

    p->format = *s;
    p->format.common.codec = enc->codec;

    ctx->bit_rate = 800000;
    ctx->bit_rate_tolerance = 2000000;
    ctx->me_method = ME_EPZS;
    ctx->frame_rate = s->video.frame_rate.num;
    ctx->frame_rate_base = s->video.frame_rate.den;
    ctx->width = s->video.width;
    ctx->height = s->video.height;
    ctx->qmin = 2;
    ctx->qmax = 31;
    ctx->max_qdiff = 3;
    ctx->max_b_frames = 0;
    if(s->video.aspect.num)
	ctx->aspect_ratio = (float) s->video.aspect.num / s->video.aspect.den;
    ctx->mb_qmin = 2;
    ctx->mb_qmax = 31;

    avcodec_open(ctx, enc->avc);

    return p->next->probe(p->next, NULL, &p->format);
}

static int
avc_encvid_flush(tcvp_pipe_t *p, int drop)
{
    avc_encvid_t *enc = p->private;

    if(drop)
	avcodec_flush_buffers(enc->ctx);

    return p->next->flush(p->next, drop);
}

static void
avc_free_encvid(void *p)
{
    tcvp_pipe_t *tp = p;
    avc_encvid_t *enc = tp->private;

    free(enc->buf);
    free(enc->frame);
    avcodec_close(enc->ctx);
    free(enc);
}

static tcvp_pipe_t *
avc_encvideo_new(char *codec)
{
    enum CodecID cid;
    AVCodec *avc;
    AVCodecContext *ctx;
    avc_encvid_t *enc;
    tcvp_pipe_t *p;

    cid = avc_codec_id(codec);
    avc = avcodec_find_encoder(cid);
    if(!avc){
	fprintf(stderr, "Can't find encoder for '%s'.\n", codec);
	return NULL;
    }

    ctx = avcodec_alloc_context();
    avcodec_get_context_defaults(ctx);

    enc = malloc(sizeof(*enc));
    enc->codec = codec;
    enc->avc = avc;
    enc->ctx = ctx;
    enc->frame = avcodec_alloc_frame();
    enc->buf = malloc(ENCBUFSIZE);

    p = tcallocdz(sizeof(*p), NULL, avc_free_encvid);
    p->input = avc_encvid;
    p->probe = avc_encvideo_probe;
    p->flush = avc_encvid_flush;
    p->private = enc;

    return p;
}

extern tcvp_pipe_t *
avc_mpeg4_enc_new(stream_t *s, conf_section *cs, timer__t *t)
{
    return avc_encvideo_new("video/mpeg4");
}

extern tcvp_pipe_t *
avc_mpeg_enc_new(stream_t *s, conf_section *cs, timer__t *t)
{
    return avc_encvideo_new("video/mpeg");
}
