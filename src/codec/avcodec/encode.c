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
    p->format.common.bit_rate = ctx->bit_rate;

    ctx->frame_rate = s->video.frame_rate.num;
    ctx->frame_rate_base = s->video.frame_rate.den;
    ctx->width = s->video.width;
    ctx->height = s->video.height;
    if(s->video.aspect.num)
	ctx->aspect_ratio = (float) s->video.aspect.num / s->video.aspect.den;
    if(s->common.flags & TCVP_STREAM_FLAG_INTERLACED)
	ctx->flags |= CODEC_FLAG_INTERLACED_DCT;
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
    if(enc->ctx->codec)
	avcodec_close(enc->ctx);
    free(enc);
}

static tcvp_pipe_t *
avc_encvideo_new(stream_t *s, char *codec, conf_section *cf)
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

#define ctx_conf(n, f) conf_getvalue(cf, #n, "%"#f, &ctx->n)
    ctx_conf(bit_rate, i);
    ctx_conf(bit_rate_tolerance, i);
    ctx_conf(flags, i);
    ctx_conf(me_method, i);
    ctx_conf(gop_size, i);
    ctx_conf(qcompress, f);
    ctx_conf(qblur, f);
    ctx_conf(qmin, i);
    ctx_conf(qmax, i);
    ctx_conf(max_qdiff, i);
    ctx_conf(max_b_frames, i);
    ctx_conf(b_quant_factor, f);
    ctx_conf(luma_elim_threshold, i);
    ctx_conf(chroma_elim_threshold, i);
    ctx_conf(b_quant_offset, f);
    ctx_conf(rc_qsquish, f);
    ctx_conf(rc_qmod_amp, f);
    ctx_conf(rc_qmod_freq, i);
    ctx_conf(rc_eq, s);
    ctx_conf(rc_max_rate, i);
    ctx_conf(rc_min_rate, i);
    ctx_conf(rc_buffer_size, i);
    ctx_conf(rc_buffer_aggressivity, f);
    ctx_conf(i_quant_factor, f);
    ctx_conf(i_quant_offset, f);
    ctx_conf(rc_initial_cplx, f);
    ctx_conf(dct_algo, i);
    ctx_conf(lumi_masking, f);
    ctx_conf(temporal_cplx_masking, f);
    ctx_conf(spatial_cplx_masking, f);
    ctx_conf(p_masking, f);
    ctx_conf(dark_masking, f);
    ctx_conf(slice_count, i);
    ctx_conf(aspect_ratio, f);
    ctx_conf(debug, i);
    ctx_conf(mb_qmin, i);
    ctx_conf(mb_qmax, i);
    ctx_conf(me_cmp, i);
    ctx_conf(me_sub_cmp, i);
    ctx_conf(mb_cmp, i);
    ctx_conf(dia_size, i);
    ctx_conf(last_predictor_count, i);
    ctx_conf(pre_me, i);
    ctx_conf(me_pre_cmp, i);
    ctx_conf(pre_dia_size, i);
    ctx_conf(me_subpel_quality, i);
    ctx_conf(me_range, i);
    ctx_conf(intra_quant_bias, i);
    ctx_conf(inter_quant_bias, i);
    ctx_conf(global_quality, i);
    ctx_conf(coder_type, i);
    ctx_conf(mb_decision, i);

#define ctx_flag(c, f) if(!conf_getvalue(cf, #c, ""))	\
    ctx->flags |= CODEC_FLAG_##f

    ctx_flag(qscale, QSCALE);
    ctx_flag(4mv, 4MV);
    ctx_flag(qpel, QPEL);
    ctx_flag(gmc, GMC);
    ctx_flag(interlaced_dct, INTERLACED_DCT);
    ctx_flag(alt_scan, ALT_SCAN);
    ctx_flag(trellis_quant, TRELLIS_QUANT);

    enc = malloc(sizeof(*enc));
    enc->codec = codec;
    enc->avc = avc;
    enc->ctx = ctx;
    enc->frame = avcodec_alloc_frame();
    enc->buf = malloc(ENCBUFSIZE);

    p = tcallocdz(sizeof(*p), NULL, avc_free_encvid);
    p->format = *s;
    p->format.common.codec = codec;
    p->input = avc_encvid;
    p->probe = avc_encvideo_probe;
    p->flush = avc_encvid_flush;
    p->private = enc;

    return p;
}

extern tcvp_pipe_t *
avc_mpeg4_enc_new(stream_t *s, conf_section *cs, tcvp_timer_t **t)
{
    return avc_encvideo_new(s, "video/mpeg4", cs);
}

extern tcvp_pipe_t *
avc_mpeg_enc_new(stream_t *s, conf_section *cs, tcvp_timer_t **t)
{
    return avc_encvideo_new(s, "video/mpeg", cs);
}
