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
#include <tcvp_types.h>
#include <ffmpeg/avcodec.h>
#include <avcodec_tc2.h>

static enum CodecID avc_codec_id(stream_t *);

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
    packet_t *in;		/* remaining data from probe */
    char *inbuf;
    size_t insize;
    packet_t *out;		/* pending output from probe */
} avc_codec_t;

static void
avc_free_packet(packet_t *p)
{
    free(p->sizes);
    free(p);
}

static int
avc_decaudio(tcvp_pipe_t *p, packet_t *pk)
{
    packet_t *out;
    avc_codec_t *ac = p->private;
    char *inbuf;
    int insize;

    if(ac->out && p->next){
	p->next->input(p->next, ac->out);
	ac->out = NULL;
    }

    if(ac->in){
	packet_t *in = ac->in;
	ac->in = NULL;
	avc_decaudio(p, in);
    }

    if(ac->inbuf){
	inbuf = ac->inbuf;
	insize = ac->insize;
	ac->inbuf = NULL;
    } else if(pk){
	inbuf = pk->data[0];
	insize = pk->sizes[0];
    } else {
	p->next->input(p->next, NULL);
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
	    out = malloc(sizeof(*out));
	    out->data = (u_char **) &out->private;
	    out->sizes = malloc(sizeof(*out->sizes));
	    out->sizes[0] = outsize;
	    out->planes = 1;
	    out->pts = 0;
	    out->free = avc_free_packet;
	    out->private = ac->buf;
	    if(p->next){
		p->next->input(p->next, out);
	    } else {
		ac->have_params = 1;
		ac->out = out;
		if(insize){
		    ac->inbuf = inbuf;
		    ac->insize = insize;
		    ac->in = pk;
		} else {
		    pk->free(pk);
		}
		return 0;
	    }
	}
    }

    pk->free(pk);

    return 0;
}

static int
avc_decvideo(tcvp_pipe_t *p, packet_t *pk)
{
    packet_t *out;
    avc_codec_t *vc = p->private;
    char *inbuf;
    int insize;

    if(vc->out && p->next){
	p->next->input(p->next, vc->out);
	vc->out = NULL;
    }

    if(vc->in){
	packet_t *in = vc->in;
	vc->in = NULL;
	avc_decvideo(p, in);
    }

    if(vc->inbuf){
	inbuf = vc->inbuf;
	insize = vc->insize;
	vc->inbuf = NULL;
    } else if(pk){
	inbuf = pk->data[0];
	insize = pk->sizes[0];
    } else {
	p->next->input(p->next, NULL);
	return 0;
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

	    out = malloc(sizeof(*out));
	    out->data = vc->frame->data;
	    out->sizes = malloc(4 * sizeof(*out->sizes));
	    for(i = 0; i < 4; i++){
		out->sizes[i] = vc->frame->linesize[i];
		if(out->sizes[i] == 0)
		    break;
	    }
	    out->planes = i;

	    if(pk->pts){
		uint64_t pts = vc->pts / vc->ptsd;
		uint64_t ptsdiff = pk->pts > pts? pk->pts - pts: pts - pk->pts;
		if(ptsdiff > 1000000){
		    vc->pts = pk->pts * vc->ptsd;
		}
	    }

	    out->pts = vc->pts / vc->ptsd;
	    vc->pts += vc->ptsn;

	    out->free = avc_free_packet;
	    out->private = NULL;
	    if(p->next){
		p->next->input(p->next, out);
	    } else {
		vc->have_params = 1;
		vc->out = out;
		if(insize){
		    vc->inbuf = inbuf;
		    vc->insize = insize;
		    vc->in = pk;
		} else {
		    pk->free(pk);
		}
		return 0;
	    }
	}
    }

    if(!pk->sizes[0])
	vc->pts += vc->ptsn;

    pk->free(pk);

    return 0;
}

static int
avc_free_adpipe(tcvp_pipe_t *p)
{
    avc_codec_t *ac = p->private;

    avcodec_close(ac->ctx);
    if(ac->buf)
	free(ac->buf);
    free(ac);
    free(p);

    return 0;
}

static int
avc_free_vdpipe(tcvp_pipe_t *p)
{
    avc_codec_t *vc = p->private;

    avcodec_close(vc->ctx);
    free(vc->frame);
    free(vc);
    free(p);

    return 0;
}

extern int
avc_probe_audio(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    avc_codec_t *ac = p->private;
    int ret;

    if(avc_decaudio(p, pk) < 0)
	return PROBE_FAIL;

    if(ac->have_params){
	s->audio.sample_rate = ac->ctx->sample_rate;
	s->audio.channels = ac->ctx->channels;
	ret = PROBE_OK;
    } else {
	ret = PROBE_AGAIN;
    }

    return ret;
}


static int pixel_fmts[] = {
    [PIX_FMT_YUV420P] = PIXEL_FORMAT_I420,
    [PIX_FMT_YUV422] = PIXEL_FORMAT_YUY2,
    [PIX_FMT_RGB24] = 0,
    [PIX_FMT_BGR24] = 0,
    [PIX_FMT_YUV422P] = 0,
    [PIX_FMT_YUV444P] = 0,
    [PIX_FMT_RGBA32] = 0,
    [PIX_FMT_YUV410P] = 0,
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

    if(avc_decvideo(p, pk) < 0)
	return PROBE_FAIL;

    if(vc->have_params){
	if(!s->video.frame_rate.num){
	    s->video.frame_rate.num = vc->ctx->frame_rate;
	    s->video.frame_rate.den = vc->ctx->frame_rate_base;
	}
	s->video.width = vc->ctx->width;
	s->video.height = vc->ctx->height;
	s->video.pixel_format = pixel_fmts[vc->ctx->pix_fmt];
	ret = PROBE_OK;
    } else {
	ret = PROBE_AGAIN;
    }

    return ret;
}

static int
avc_flush(tcvp_pipe_t *p, int drop)
{
    avc_codec_t *ac = p->private;

    if(drop){
	if(ac->in)
	    ac->in->free(ac->in);
	if(ac->out)
	    ac->out->free(ac->out);

	ac->in = NULL;
	ac->out = NULL;
	ac->inbuf = NULL;
	ac->insize = 0;

	avcodec_flush_buffers(ac->ctx);
    }

    return p->next->flush(p->next, drop);
}

extern tcvp_pipe_t *
avc_new(stream_t *s, int mode)
{
    tcvp_pipe_t *p = NULL;
    avc_codec_t *ac, *vc;
    AVCodec *avc = NULL;
    AVCodecContext *avctx;
    enum CodecID id;

    id = avc_codec_id(s);
    if(mode == CODEC_MODE_DECODE)
	avc = avcodec_find_decoder(id);

    if(avc == NULL){
	fprintf(stderr, "AVC: Can't find codec for '%s' => %i\n",
		s->common.codec, id);
	return NULL;
    }

    avctx = avcodec_alloc_context();
    avcodec_get_context_defaults(avctx);
    if(id == CODEC_ID_MPEG1VIDEO)
	avctx->flags |= CODEC_FLAG_TRUNCATED;

    switch(s->stream_type){
    case STREAM_TYPE_AUDIO:
	avctx->sample_rate = s->audio.sample_rate;
	avctx->channels = s->audio.channels;
	avctx->bit_rate = s->audio.bit_rate;
	avctx->block_align = s->audio.block_align;

	ac = calloc(1, sizeof(*ac));
	ac->ctx = avctx;
	ac->buf = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

	p = calloc(1, sizeof(*p));
	if(mode == CODEC_MODE_DECODE)
	    p->input = avc_decaudio;
	p->start = NULL;
	p->stop = NULL;
	p->free = avc_free_adpipe;
	p->probe = avc_probe_audio;
	p->private = ac;
	break;

    case STREAM_TYPE_VIDEO:
	avctx->width = s->video.width;
	avctx->height = s->video.height;
	avctx->frame_rate = s->video.frame_rate.num;
	avctx->frame_rate_base = s->video.frame_rate.den;
	avctx->workaround_bugs = 0x3ff;
	avctx->error_resilience = FF_ER_AGGRESSIVE;
	avctx->error_concealment = 3;

	vc = calloc(1, sizeof(*vc));
	vc->ctx = avctx;
	vc->ptsn = (uint64_t) 1000000 * s->video.frame_rate.den;
	vc->ptsd = s->video.frame_rate.num;
	vc->pts = 0;
	vc->frame = avcodec_alloc_frame();

	p = calloc(1, sizeof(*p));
	if(mode == CODEC_MODE_DECODE)
	    p->input = avc_decvideo;
	p->start = NULL;
	p->stop = NULL;
	p->free = avc_free_vdpipe;
	p->probe = avc_probe_video;
	p->private = vc;
	break;
    }

    avctx->extradata = s->common.codec_data;
    avctx->extradata_size = s->common.codec_data_size;

    p->flush = avc_flush;

    avcodec_open(avctx, avc);

    return p;
}

static const char *codec_names[] = {
    [CODEC_ID_NONE] = "", 
    [CODEC_ID_MPEG1VIDEO] = "video/mpeg",
    [CODEC_ID_H263] = "video/h263",
    [CODEC_ID_RV10] = "video/rv10",
    [CODEC_ID_MP2] = "audio/mp2",
    [CODEC_ID_MP3LAME] = "audio/mp3",
    [CODEC_ID_VORBIS] = "audio/vorbis",
    [CODEC_ID_AC3] = "audio/ac3",
    [CODEC_ID_MJPEG] = "video/mjpeg",
    [CODEC_ID_MJPEGB] = "video/mjpegb",
    [CODEC_ID_MPEG4] = "video/mpeg4",
    [CODEC_ID_RAWVIDEO] = "video/rawvideo",
    [CODEC_ID_MSMPEG4V1] = "video/msmpeg4v1",
    [CODEC_ID_MSMPEG4V2] = "video/msmpeg4v2",
    [CODEC_ID_MSMPEG4V3] = "video/msmpeg4v3",
    [CODEC_ID_WMV1] = "video/wmv1",
    [CODEC_ID_WMV2] = "video/wmv2",
    [CODEC_ID_H263P] = "video/h263p",
    [CODEC_ID_H263I] = "video/h263i",
    [CODEC_ID_SVQ1] = "video/svq1",
    [CODEC_ID_SVQ3] = "video/svq3",
    [CODEC_ID_DVVIDEO] = "video/dv",
    [CODEC_ID_DVAUDIO] = "audio/dv",
    [CODEC_ID_WMAV1] = "audio/wmav1",
    [CODEC_ID_WMAV2] = "audio/wmav2",
    [CODEC_ID_MACE3] = "audio/mace3",
    [CODEC_ID_MACE6] = "audio/mace6",
    [CODEC_ID_HUFFYUV] = "video/huffyuv",
    [CODEC_ID_CYUV] = "video/cyuv",
    [CODEC_ID_H264] = "video/h264",
    [CODEC_ID_INDEO3] = "video/indeo3",
    [CODEC_ID_VP3] = "video/vp3",

    /* various pcm "codecs" */
    [CODEC_ID_PCM_S16LE] = "audio/pcm-s16le",
    [CODEC_ID_PCM_S16BE] = "audio/pcm-s16be",
    [CODEC_ID_PCM_U16LE] = "audio/pcm-u16le",
    [CODEC_ID_PCM_U16BE] = "audio/pcm-u16be",
    [CODEC_ID_PCM_S8] = "audio/pcm-s8",
    [CODEC_ID_PCM_U8] = "audio/pcm-u8",
    [CODEC_ID_PCM_MULAW] = "audio/pcm-ulaw",
    [CODEC_ID_PCM_ALAW] = "audio/pcm-alaw",

    /* various adpcm codecs */
    [CODEC_ID_ADPCM_IMA_QT] = "audio/adpcm-ima-qt",
    [CODEC_ID_ADPCM_IMA_WAV] = "audio/adpcm-ima-wav",
    [CODEC_ID_ADPCM_MS] = "audio/adpcm-ms",
    NULL
};

static enum CodecID
avc_codec_id(stream_t *s)
{
    int i;
    char *n = s->common.codec;

    for(i = 0; i < sizeof(codec_names)/sizeof(codec_names[0]); i++){
	if(codec_names[i] && !strcmp(n, codec_names[i])){
	    return i;
	}
    }

    return 0;
}
