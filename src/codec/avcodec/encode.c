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

extern int
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
	ep = tcallocz(sizeof(*ep));
	ep->pk.stream = pk->stream;
	ep->pk.data = &ep->data;
	ep->pk.sizes = &ep->size;
	ep->pk.planes = 1;
	ep->pk.flags = TCVP_PKT_FLAG_PTS;
	if(f->key_frame)
	    ep->pk.flags |= TCVP_PKT_FLAG_KEY;
	ep->pk.pts = pk->pts;
	ep->data = enc->buf;
	ep->size = size;
	p->next->input(p->next, &ep->pk);
    }

    tcfree(pk);

    return 0;
}

extern int
avc_encvideo_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    avc_encvid_t *enc = p->private;
    AVCodecContext *ctx = enc->ctx;

    p->format.common.codec = enc->codec;
    p->format.common.bit_rate = ctx->bit_rate;

    ctx->frame_rate = s->video.frame_rate.num;
    ctx->frame_rate_base = s->video.frame_rate.den;
    ctx->width = s->video.width;
    ctx->height = s->video.height;
    if(s->video.aspect.num){
#if LIBAVCODEC_BUILD >= 4687
	tcfraction_t asp = { s->video.height * s->video.aspect.num,
			     s->video.width * s->video.aspect.den };
	tcreduce(&asp);
	if(asp.num > 255 || asp.den > 255){
	    double a = (double) asp.num / asp.den;
	    if(asp.num > asp.den){
		asp.num = 240;
		asp.den = asp.num / a + 0.5;
	    } else {
		asp.den = 240;
		asp.num = asp.den * a + 0.5;
	    }
	    tcreduce(&asp);
	}
	ctx->sample_aspect_ratio.num = asp.num;
	ctx->sample_aspect_ratio.den = asp.den;
	tc2_print("AVCODEC", TC2_PRINT_DEBUG,
		  "SAR = %i/%i\n", asp.num, asp.den);
#else
	ctx->aspect_ratio = (float) s->video.aspect.num / s->video.aspect.den;
#endif
    }
    if(s->common.flags & TCVP_STREAM_FLAG_INTERLACED)
	ctx->flags |= CODEC_FLAG_INTERLACED_DCT;
    avcodec_open(ctx, enc->avc);

    return PROBE_OK;
}

extern int
avc_encvid_flush(tcvp_pipe_t *p, int drop)
{
    avc_encvid_t *enc = p->private;

    if(drop && enc->ctx->codec)
	avcodec_flush_buffers(enc->ctx);

    return 0;
}

static void
avc_free_encvid(void *p)
{
    avc_encvid_t *enc = p;

    free(enc->buf);
    free(enc->frame);
    if(enc->ctx->codec)
	avcodec_close(enc->ctx);
}

extern int
avc_encvideo_new(tcvp_pipe_t *p, stream_t *s, char *codec,
		 tcconf_section_t *cf)
{
    enum CodecID cid;
    AVCodec *avc;
    AVCodecContext *ctx;
    avc_encvid_t *enc;

    cid = avc_codec_id(codec);
    avc = avcodec_find_encoder(cid);
    if(!avc){
	tc2_print("AVCODEC", TC2_PRINT_ERROR, "Can't find encoder for '%s'.\n", codec);
	return -1;
    }

    ctx = avcodec_alloc_context();
    avcodec_get_context_defaults(ctx);

#define ctx_conf(n, f) tcconf_getvalue(cf, #n, "%"#f, &ctx->n)
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
#if LIBAVCODEC_BUILD >= 4684
    ctx_conf(lmin, i);
    ctx_conf(lmax, i);
#endif
#if LIBAVCODEC_BUILD >= 4690
    ctx_conf(noise_reduction, i);
#endif
#if LIBAVCODEC_BUILD >= 4694
    ctx_conf(rc_initial_buffer_occupancy, i);
    ctx_conf(inter_threshold, i);
#endif
#if LIBAVCODEC_BUILD >= 4700
    ctx_conf(quantizer_noise_shaping, i);
#endif

#define ctx_flag(c, f) if(!tcconf_getvalue(cf, #c, ""))	\
    ctx->flags |= CODEC_FLAG_##f

    ctx_flag(qscale, QSCALE);
    ctx_flag(4mv, 4MV);
    ctx_flag(qpel, QPEL);
    ctx_flag(gmc, GMC);
    ctx_flag(interlaced_dct, INTERLACED_DCT);
    ctx_flag(alt_scan, ALT_SCAN);
    ctx_flag(trellis_quant, TRELLIS_QUANT);
#if LIBAVCODEC_BUILD >= 4681
    ctx_flag(cbp_rd, CBP_RD);
#endif
#if LIBAVCODEC_BUILD >= 4682
    ctx_flag(ac_pred, AC_PRED);
#endif
#if LIBAVCODEC_BUILD >= 4683
    ctx_flag(mv0, MV0);
#endif
#if LIBAVCODEC_BUILD >= 4693
    ctx_flag(qp_rd, QP_RD);
#endif
#if LIBAVCODEC_BUILD >= 4694
    ctx_flag(obmc, OBMC);
    ctx_flag(loop_filter, LOOP_FILTER);
#endif
#if LIBAVCODEC_BUILD >= 4698
    ctx_flag(interlaced_me, INTERLACED_ME);
#endif
#if LIBAVCODEC_BUILD >= 4700
    ctx_flag(closed_gop, CLOSED_GOP);
#endif

    enc = tcallocdz(sizeof(*enc), NULL, avc_free_encvid);
    enc->codec = codec;
    enc->avc = avc;
    enc->ctx = ctx;
    enc->frame = avcodec_alloc_frame();
    enc->buf = malloc(ENCBUFSIZE);

    p->format = *s;
    p->format.common.codec = codec;
    p->private = enc;

    return 0;
}

#define avc_enc_new(cd)							\
extern int								\
avc_##cd##_enc_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,	\
		      tcvp_timer_t *t, muxed_stream_t *ms)		\
{									\
    return avc_encvideo_new(p, s, "video/"#cd, cs);			\
}

avc_enc_new(mpeg4)
avc_enc_new(mpeg2)
avc_enc_new(mpeg)
