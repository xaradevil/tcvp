/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

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
#include <tcvp.h>
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


typedef struct avc_audio_codec {
    AVCodecContext *ctx;
    tcvp_pipe_t *out;
    char *buf;
} avc_audio_codec_t;

typedef struct avc_video_codec {
    AVCodecContext *ctx;
    uint64_t dpts, last_pts;
    tcvp_pipe_t *out;
    AVFrame *frame;
} avc_video_codec_t;

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
    avc_audio_codec_t *ac = p->private;
    char *inbuf = pk->data[0];
    int insize = pk->sizes[0];

    while(insize > 0){
	int l, outsize;

	l = avcodec_decode_audio(ac->ctx, (int16_t *) ac->buf, &outsize,
				 inbuf, insize);
	if(l < 0){
	    return -1;
	}

	if(outsize > 0){
	    out = malloc(sizeof(*out));
	    out->data = (u_char **) &out->private;
	    out->sizes = malloc(sizeof(size_t));
	    out->sizes[0] = outsize;
	    out->planes = 1;
	    out->pts = 0;
	    out->free = avc_free_packet;
	    out->private = ac->buf;
	    ac->out->input(ac->out, out);
	}

	inbuf += l;
	insize -= l;
    }

    pk->free(pk);

    return 0;
}

static int
avc_decvideo(tcvp_pipe_t *p, packet_t *pk)
{
    packet_t *out;
    avc_video_codec_t *vc = p->private;
    char *inbuf = pk->data[0];
    int insize = pk->sizes[0];

    while(insize > 0){
	int l, gp = 0;

	l = avcodec_decode_video(vc->ctx, vc->frame, &gp, inbuf, insize);

	if(l < 0)
	    return -1;

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
	    if(vc->frame->pts)
		out->pts = vc->frame->pts;
	    else
		out->pts = vc->last_pts + vc->dpts;
	    vc->last_pts = out->pts;
	    out->free = avc_free_packet;
	    out->private = NULL;
	    vc->out->input(vc->out, out);
	}

	inbuf += l;
	insize -= l;
    }

    pk->free(pk);

    return 0;
}

static int
avc_free_adpipe(tcvp_pipe_t *p)
{
    avc_audio_codec_t *ac = p->private;

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
    avc_video_codec_t *vc = p->private;

    avcodec_close(vc->ctx);
    free(vc->frame);
    free(vc);
    free(p);

    return 0;
}

extern tcvp_pipe_t *
avc_new(stream_t *s, int mode, tcvp_pipe_t *out)
{
    tcvp_pipe_t *p = NULL;
    avc_audio_codec_t *ac;
    avc_video_codec_t *vc;
    AVCodec *avc = NULL;
    AVCodecContext *avctx;
    enum CodecID id;

    id = avc_codec_id(s);
    if(mode == CODEC_MODE_DECODE)
	avc = avcodec_find_decoder(id);

    if(avc == NULL)
	return NULL;

    avctx = avcodec_alloc_context();
    avcodec_get_context_defaults(avctx);

    switch(s->stream_type){
    case STREAM_TYPE_AUDIO:
	ac = malloc(sizeof(*ac));
	ac->ctx = avctx;
	ac->out = out;
	ac->buf = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

	p = malloc(sizeof(*p));
	if(mode == CODEC_MODE_DECODE)
	    p->input = avc_decaudio;
	p->start = NULL;
	p->stop = NULL;
	p->free = avc_free_adpipe;
	p->private = ac;
	break;

    case STREAM_TYPE_VIDEO:
	avctx->width = s->video.width;
	avctx->height = s->video.height;
	avctx->frame_rate = s->video.frame_rate * FRAME_RATE_BASE;

	vc = malloc(sizeof(*vc));
	vc->ctx = avctx;
	vc->dpts = 1000000 / s->video.frame_rate;
	vc->last_pts = 0;
	vc->out = out;
	vc->frame = avcodec_alloc_frame();

	p = malloc(sizeof(*p));
	if(mode == CODEC_MODE_DECODE)
	    p->input = avc_decvideo;
	p->start = NULL;
	p->stop = NULL;
	p->free = avc_free_vdpipe;
	p->private = vc;
	break;
    }

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
    [CODEC_ID_DVVIDEO] = "video/dv",
    [CODEC_ID_DVAUDIO] = "audio/dv",
    [CODEC_ID_WMAV1] = "audio/wmav1",
    [CODEC_ID_WMAV2] = "audio/wmav2",
    [CODEC_ID_MACE3] = "audio/mace3",
    [CODEC_ID_MACE6] = "audio/mace6",
    [CODEC_ID_HUFFYUV] = "video/huffyuv",
    [CODEC_ID_CYUV] = "video/cyuv",

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
    char *n;

    switch(s->stream_type){
    case STREAM_TYPE_VIDEO:
	n = s->video.codec;
	break;
    case STREAM_TYPE_AUDIO:
	n = s->audio.codec;
	break;
    default:
	return 0;
    }

    for(i = 0; codec_names[i]; i++){
	if(!strcmp(n, codec_names[i])){
	    return i;
	}
    }

    return 0;
}