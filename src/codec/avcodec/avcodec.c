/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
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

extern int
avc_init(char *arg)
{
    avcodec_init();
    avcodec_register_all();
    return 0;
}


typedef struct avc_codec {
    AVCodecContext *ctx;

    /* audio */
    char *buf;

    /* video */
    uint64_t pts;
    uint64_t ptsn, ptsd;
    AVFrame *frame;

    int have_params;
} avc_codec_t;

static void
avc_free_packet(void *v)
{
    packet_t *p = v;
    free(p->sizes);
}

static int
do_decaudio(tcvp_pipe_t *p, packet_t *pk, int probe)
{
    packet_t *out;
    avc_codec_t *ac = p->private;
    char *inbuf;
    int insize;

    if(pk->data){
	inbuf = pk->data[0];
	insize = pk->sizes[0];
    } else {
	p->next->input(p->next, pk);
	return 0;
    }

    while(insize > 0){
	int l, outsize;

	l = avcodec_decode_audio(ac->ctx, (int16_t *) ac->buf, &outsize,
				 inbuf, insize);
	if(l < 0){
	    return -1;
	}

	inbuf += l;
	insize -= l;

	if(outsize > 0){
	    if(probe){
		ac->have_params = 1;
		break;
	    }

	    out = tcallocd(sizeof(*out), NULL, avc_free_packet);
	    out->data = (u_char **) &out->private;
	    out->sizes = malloc(sizeof(*out->sizes));
	    out->sizes[0] = outsize;
	    out->planes = 1;
	    out->pts = pk->pts;
	    pk->pts = 0;
	    out->private = ac->buf;
	    p->next->input(p->next, out);
	}
    }

    tcfree(pk);

    return 0;
}

static int
avc_decaudio(tcvp_pipe_t *p, packet_t *pk)
{
    return do_decaudio(p, pk, 0);
}

static int
do_decvideo(tcvp_pipe_t *p, packet_t *pk, int probe)
{
    packet_t *out;
    avc_codec_t *vc = p->private;
    char *inbuf;
    int insize;

    if(pk->data){
	inbuf = pk->data[0];
	insize = pk->sizes[0];
    } else {
	p->next->input(p->next, pk);
	return 0;
    }

    if(pk->flags & TCVP_PKT_FLAG_PTS){
	vc->pts = pk->pts * vc->ptsd;
    }

    while(insize > 0){
	int l, gp = 0;

	l = avcodec_decode_video(vc->ctx, vc->frame, &gp, inbuf, insize);

	if(l < 0)
	    return -1;

	inbuf += l;
	insize -= l;

	if(gp){
	    int i;

	    if(probe){
		vc->have_params = 1;
		break;
	    }

	    out = tcallocd(sizeof(*out), NULL, avc_free_packet);
	    out->stream = pk->stream;
	    out->data = vc->frame->data;
	    out->sizes = malloc(4 * sizeof(*out->sizes));
	    for(i = 0; i < 4; i++){
		out->sizes[i] = vc->frame->linesize[i];
		if(out->sizes[i] == 0)
		    break;
	    }
	    out->planes = i;

	    out->flags = TCVP_PKT_FLAG_PTS;
	    out->pts = vc->pts / vc->ptsd;
	    vc->pts += vc->ptsn;

	    out->private = NULL;
	    p->next->input(p->next, out);
	}
    }

    tcfree(pk);

    return 0;
}

static int
avc_decvideo(tcvp_pipe_t *p, packet_t *pk)
{
    return do_decvideo(p, pk, 0);
}

static void
avc_free_apipe(void *p)
{
    tcvp_pipe_t *tp = p;
    avc_codec_t *ac = tp->private;
    avcodec_close(ac->ctx);
    if(ac->buf)
	free(ac->buf);
    free(ac);
}

static void
avc_free_vpipe(void *p)
{
    tcvp_pipe_t *tp = p;
    avc_codec_t *vc = tp->private;
    avcodec_close(vc->ctx);
    free(vc->frame);
    free(vc);
}

extern int
avc_probe_audio(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    avc_codec_t *ac = p->private;
    int ret;

    if(do_decaudio(p, pk, 1) < 0)
	return PROBE_FAIL;

    if(ac->have_params){
	p->format = *s;
	p->format.audio.codec = "audio/pcm-s16" TC2_ENDIAN;
	p->format.audio.sample_rate = ac->ctx->sample_rate;
	p->format.audio.channels = ac->ctx->channels;
	ret = p->next->probe(p->next, NULL, &p->format);
    } else {
	ret = PROBE_AGAIN;
    }

    return ret;
}


static char *pixel_fmts[] = {
    [PIX_FMT_YUV420P] = "video/raw-i420",
    [PIX_FMT_YUV422] = "video/raw-yuy2",
    [PIX_FMT_RGB24] = 0,
    [PIX_FMT_BGR24] = 0,
    [PIX_FMT_YUV422P] = "video/raw-yuv422p",
    [PIX_FMT_YUV444P] = 0,
    [PIX_FMT_RGBA32] = 0,
    [PIX_FMT_YUV410P] = "video/raw-yvu9",
    [PIX_FMT_YUV411P] = 0,
    [PIX_FMT_RGB565] = 0,
    [PIX_FMT_RGB555] = 0,
    [PIX_FMT_GRAY8] = 0,
    [PIX_FMT_MONOWHITE] = 0,
    [PIX_FMT_MONOBLACK] = 0,
    [PIX_FMT_PAL8] = 0,
    [PIX_FMT_NB] = 0,
};

extern int
avc_probe_video(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    avc_codec_t *vc = p->private;
    int ret;

    if(!pk){
	p->format = *s;
	p->format.video.codec = "video/raw-i420";
	ret = p->next->probe(p->next, NULL, &p->format);
	goto out;
    }

    if(do_decvideo(p, pk, 1) < 0){
	ret = PROBE_AGAIN;
	goto out;
    }

    if(vc->have_params){
	if(!vc->ctx->width){
	    ret = PROBE_AGAIN;
	    goto out;
	}

	if(!pixel_fmts[vc->ctx->pix_fmt]){
	    fprintf(stderr, "avcodec: unknown pixel format %i\n",
		    vc->ctx->pix_fmt);
	    ret = PROBE_FAIL;
	    goto out;
	}

	p->format = *s;
	p->format.video.codec = pixel_fmts[vc->ctx->pix_fmt];
	p->format.video.width = vc->ctx->width;
	p->format.video.height = vc->ctx->height;
	if(vc->ctx->frame_rate){
	    vc->ptsn = (uint64_t) 27000000 * vc->ctx->frame_rate_base;
	    vc->ptsd = vc->ctx->frame_rate;
	    p->format.video.frame_rate.num = vc->ctx->frame_rate;
	    p->format.video.frame_rate.den = vc->ctx->frame_rate_base;
	} else {
	    vc->ptsn = 27000000;
	    vc->ptsd = 25;
	    p->format.video.frame_rate.num = 25;
	    p->format.video.frame_rate.den = 1;
	}
	tcreduce(&p->format.video.frame_rate);
	if(vc->ctx->aspect_ratio){
	    p->format.video.aspect.num =
		vc->ctx->height * vc->ctx->aspect_ratio;
	    p->format.video.aspect.den = vc->ctx->height;
	    tcreduce(&p->format.video.aspect);
	}
	ret = p->next->probe(p->next, NULL, &p->format);
    } else {
	ret = PROBE_AGAIN;
    }

out:
    return ret;
}

static int
avc_flush(tcvp_pipe_t *p, int drop)
{
    avc_codec_t *ac = p->private;

    if(drop)
	avcodec_flush_buffers(ac->ctx);

    return p->next->flush(p->next, drop);
}

extern tcvp_pipe_t *
avc_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    tcvp_pipe_t *p = NULL;
    avc_codec_t *ac, *vc;
    AVCodec *avc = NULL;
    AVCodecContext *avctx;
    enum CodecID id;

    id = avc_codec_id(s->common.codec);
    avc = avcodec_find_decoder(id);

    if(avc == NULL){
	fprintf(stderr, "AVC: Can't find codec for '%s' => %i\n",
		s->common.codec, id);
	return NULL;
    }

    avctx = avcodec_alloc_context();
    if(avc->capabilities & CODEC_CAP_TRUNCATED)
	avctx->flags |= CODEC_FLAG_TRUNCATED;

#define ctx_conf(n, f) tcconf_getvalue(cs, #n, "%"#f, &avctx->n)
    ctx_conf(workaround_bugs, i);
    ctx_conf(error_resilience, i);
    ctx_conf(error_concealment, i);
    ctx_conf(idct_algo, i);
    ctx_conf(debug, i);

    switch(s->stream_type){
    case STREAM_TYPE_AUDIO:
	avctx->sample_rate = s->audio.sample_rate;
	avctx->channels = s->audio.channels;
	avctx->bit_rate = s->audio.bit_rate;
	avctx->block_align = s->audio.block_align;

	ac = calloc(1, sizeof(*ac));
	ac->ctx = avctx;
	ac->buf = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

	p = tcallocdz(sizeof(*p), NULL, avc_free_apipe);
	p->format = *s;
	p->format.video.codec = "audio/pcm-s16";
	p->input = avc_decaudio;
	p->probe = avc_probe_audio;
	p->private = ac;
	break;

    case STREAM_TYPE_VIDEO:
	avctx->width = s->video.width;
	avctx->height = s->video.height;
	avctx->frame_rate = s->video.frame_rate.num;
	avctx->frame_rate_base = s->video.frame_rate.den;

	vc = calloc(1, sizeof(*vc));
	vc->ctx = avctx;
	vc->ptsn = (uint64_t) 27000000 * s->video.frame_rate.den;
	vc->ptsd = s->video.frame_rate.num?: 1;
	vc->pts = 0;
	vc->frame = avcodec_alloc_frame();

	p = tcallocdz(sizeof(*p), NULL, avc_free_vpipe);
	p->format = *s;
	p->format.video.codec = "video/yuv-420";
	p->input = avc_decvideo;
	p->probe = avc_probe_video;
	p->private = vc;
	break;

    default:
	fprintf(stderr, "AVCODEC: unknown stream type %i\n", s->stream_type);
	return NULL;
    }

    avctx->extradata = s->common.codec_data;
    avctx->extradata_size = s->common.codec_data_size;

    p->flush = avc_flush;

    avcodec_open(avctx, avc);

    return p;
}

static char *codec_names[][2] = {
    { (char *) CODEC_ID_NONE, "" }, 
    { (char *) CODEC_ID_MPEG1VIDEO, "video/mpeg" },
    { (char *) CODEC_ID_MPEG2VIDEO, "video/mpeg2" },
    { (char *) CODEC_ID_H263, "video/h263" },
    { (char *) CODEC_ID_RV10, "video/rv10" },
    { (char *) CODEC_ID_MP3, "audio/mp2" },
    { (char *) CODEC_ID_MP3, "audio/mp3" },
    { (char *) CODEC_ID_MP3, "audio/mpeg" },
    { (char *) CODEC_ID_VORBIS, "audio/vorbis" },
    { (char *) CODEC_ID_AC3, "audio/ac3" },
    { (char *) CODEC_ID_MJPEG, "video/mjpeg" },
    { (char *) CODEC_ID_MJPEGB, "video/mjpegb" },
    { (char *) CODEC_ID_MPEG4, "video/mpeg4" },
    { (char *) CODEC_ID_RAWVIDEO, "video/rawvideo" },
    { (char *) CODEC_ID_MSMPEG4V1, "video/msmpeg4v1" },
    { (char *) CODEC_ID_MSMPEG4V2, "video/msmpeg4v2" },
    { (char *) CODEC_ID_MSMPEG4V3, "video/msmpeg4v3" },
    { (char *) CODEC_ID_WMV1, "video/wmv1" },
    { (char *) CODEC_ID_WMV2, "video/wmv2" },
    { (char *) CODEC_ID_H263P, "video/h263p" },
    { (char *) CODEC_ID_H263I, "video/h263i" },
    { (char *) CODEC_ID_SVQ1, "video/svq1" },
    { (char *) CODEC_ID_SVQ3, "video/svq3" },
    { (char *) CODEC_ID_DVVIDEO, "video/dv" },
    { (char *) CODEC_ID_DVAUDIO, "audio/dv" },
    { (char *) CODEC_ID_WMAV1, "audio/wmav1" },
    { (char *) CODEC_ID_WMAV2, "audio/wmav2" },
    { (char *) CODEC_ID_MACE3, "audio/mace3" },
    { (char *) CODEC_ID_MACE6, "audio/mace6" },
    { (char *) CODEC_ID_HUFFYUV, "video/huffyuv" },
    { (char *) CODEC_ID_CYUV, "video/cyuv" },
    { (char *) CODEC_ID_H264, "video/h264" },
    { (char *) CODEC_ID_INDEO3, "video/indeo3" },
    { (char *) CODEC_ID_VP3, "video/vp3" },

    /* various pcm "codecs" */
    { (char *) CODEC_ID_PCM_S16LE, "audio/pcm-s16le" },
    { (char *) CODEC_ID_PCM_S16BE, "audio/pcm-s16be" },
    { (char *) CODEC_ID_PCM_U16LE, "audio/pcm-u16le" },
    { (char *) CODEC_ID_PCM_U16BE, "audio/pcm-u16be" },
    { (char *) CODEC_ID_PCM_S8, "audio/pcm-s8" },
    { (char *) CODEC_ID_PCM_U8, "audio/pcm-u8" },
    { (char *) CODEC_ID_PCM_MULAW, "audio/pcm-ulaw" },
    { (char *) CODEC_ID_PCM_ALAW, "audio/pcm-alaw" },

    /* various adpcm codecs */
    { (char *) CODEC_ID_ADPCM_IMA_QT, "audio/adpcm-ima-qt" },
    { (char *) CODEC_ID_ADPCM_IMA_WAV, "audio/adpcm-ima-wav" },
    { (char *) CODEC_ID_ADPCM_MS, "audio/adpcm-ms" },
    { NULL, NULL }
};

extern enum CodecID
avc_codec_id(char *codec)
{
    int i;

    for(i = 0; codec_names[i][1]; i++){
	if(!strcmp(codec, codec_names[i][1])){
	    return (enum CodecID) codec_names[i][0];
	}
    }

    return 0;
}

extern char *
avc_codec_name(enum CodecID id)
{
    int i;

    for(i = 0; codec_names[i][1]; i++){
	if((enum CodecID) codec_names[i][0] == id){
	    return codec_names[i][1];
	}
    }

    return 0;
}
