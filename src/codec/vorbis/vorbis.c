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
#include <tcvp.h>
#include <vorbis/vorbisfile.h>
#include <vorbis_tc2.h>

typedef struct {
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    tcvp_pipe_t *out;
    char *buf;
} VorbisContext_t;

static void
vorbis_free_packet(packet_t *p)
{
    free(p->sizes);
    free(p);
}


static inline int conv(int samples, float **pcm, char *buf, int channels) {
    int i, j, val ;
    ogg_int16_t *ptr, *data = (ogg_int16_t*)buf ;
    float *mono ;
 
    for(i = 0 ; i < channels ; i++){
	ptr = &data[i];
	mono = pcm[i] ;
	
	for(j = 0 ; j < samples ; j++) {
	    
	    val = mono[j] * 32767.f;
	    
	    if(val > 32767) val = 32767 ;
	    if(val < -32768) val = -32768 ;
	   	    
	    *ptr = val ;
	    ptr += channels;
	}
    }
    
    return 0 ;
}


static int
vorbis_decode(tcvp_pipe_t *p, packet_t *pk)
{
    packet_t *out;
    VorbisContext_t *vc = p->private;
    int samples, total_samples, total_bytes;
    float **pcm ;
    ogg_packet *op=(ogg_packet*)pk->data[0];

    if(op->packetno < 3) {
	vorbis_synthesis_headerin(&vc->vi, &vc->vc, op);
    } else {
	if(op->packetno == 3) {
	    fprintf(stderr, "Channels: %d Rate:%dHz\n", vc->vi.channels,
		    vc->vi.rate);
	    vorbis_synthesis_init(&vc->vd, &vc->vi) ;
	    vorbis_block_init(&vc->vd, &vc->vb); 
	}

	if(vorbis_synthesis(&vc->vb, op) == 0)
	    vorbis_synthesis_blockin(&vc->vd, &vc->vb) ;
    
	total_samples = 0;
	total_bytes = 0;

	while((samples = vorbis_synthesis_pcmout(&vc->vd, &pcm)) > 0) {
	    conv(samples, pcm, (char*)vc->buf + total_bytes, vc->vi.channels);
	    total_bytes += samples * 2 * vc->vi.channels;
	    total_samples += samples;
	    vorbis_synthesis_read(&vc->vd, samples);
	}

	out = malloc(sizeof(*out));
	out->data = (u_char **) &out->private;
	out->sizes = malloc(sizeof(size_t));
	out->sizes[0] = total_bytes;
	out->planes = 1;
	out->pts = 0;
	out->free = vorbis_free_packet;
	out->private = vc->buf;
	vc->out->input(vc->out, out);
    }
    pk->free(pk);

    return 0;
}


static int
vorbis_free_pipe(tcvp_pipe_t *p)
{
    VorbisContext_t *vc = p->private;

    vorbis_block_clear(&vc->vb);
    vorbis_dsp_clear(&vc->vd);
        vorbis_comment_clear(&vc->vc);
    vorbis_info_clear(&vc->vi);

    if(vc->buf)
	free(vc->buf);
    free(vc);
    free(p);

    return 0;
}


extern tcvp_pipe_t *
vorbis_new(stream_t *s, int mode, tcvp_pipe_t *out)
{
    tcvp_pipe_t *p = NULL;
    VorbisContext_t *vc;

    if(mode != CODEC_MODE_DECODE)
	return NULL;

    vc = malloc(sizeof(VorbisContext_t));

    vorbis_info_init(&vc->vi) ;
    vorbis_comment_init(&vc->vc) ;

    vc->out=out;

    vc->buf=malloc(131072);

    p = malloc(sizeof(*p));
    if(mode == CODEC_MODE_DECODE)
	p->input = vorbis_decode;
    p->start = NULL;
    p->stop = NULL;
    p->free = vorbis_free_pipe;
    p->private = vc;

    return p;
}


/* static enum CodecID avc_codec_id(stream_t *); */

/* extern int */
/* avc_init(char *arg) */
/* { */
/*     avcodec_init(); */
/*     avcodec_register_all(); */
/*     return 0; */
/* } */


/* typedef struct avc_audio_codec { */
/*     AVCodecContext *ctx; */
/*     tcvp_pipe_t *out; */
/*     char *buf; */
/* } avc_audio_codec_t; */

/* typedef struct avc_video_codec { */
/*     AVCodecContext *ctx; */
/*     uint64_t dpts, last_pts; */
/*     tcvp_pipe_t *out; */
/*     AVFrame *frame; */
/* } avc_video_codec_t; */

/* static void */
/* avc_free_packet(packet_t *p) */
/* { */
/*     free(p->sizes); */
/*     free(p); */
/* } */

/* static int */
/* avc_decaudio(tcvp_pipe_t *p, packet_t *pk) */
/* { */
/*     packet_t *out; */
/*     avc_audio_codec_t *ac = p->private; */
/*     char *inbuf = pk->data[0]; */
/*     int insize = pk->sizes[0]; */

/*     while(insize > 0){ */
/* 	int l, outsize; */

/* 	l = avcodec_decode_audio(ac->ctx, (int16_t *) ac->buf, &outsize, */
/* 				 inbuf, insize); */
/* 	if(l < 0){ */
/* 	    return -1; */
/* 	} */

/* 	if(outsize > 0){ */
/* 	    out = malloc(sizeof(*out)); */
/* 	    out->data = (u_char **) &out->private; */
/* 	    out->sizes = malloc(sizeof(size_t)); */
/* 	    out->sizes[0] = outsize; */
/* 	    out->planes = 1; */
/* 	    out->pts = 0; */
/* 	    out->free = avc_free_packet; */
/* 	    out->private = ac->buf; */
/* 	    ac->out->input(ac->out, out); */
/* 	} */

/* 	inbuf += l; */
/* 	insize -= l; */
/*     } */

/*     pk->free(pk); */

/*     return 0; */
/* } */

/* static int */
/* avc_decvideo(tcvp_pipe_t *p, packet_t *pk) */
/* { */
/*     packet_t *out; */
/*     avc_video_codec_t *vc = p->private; */
/*     char *inbuf = pk->data[0]; */
/*     int insize = pk->sizes[0]; */

/*     while(insize > 0){ */
/* 	int l, gp = 0; */

/* 	l = avcodec_decode_video(vc->ctx, vc->frame, &gp, inbuf, insize); */

/* 	if(l < 0) */
/* 	    return -1; */

/* 	if(gp){ */
/* 	    int i; */

/* 	    out = malloc(sizeof(*out)); */
/* 	    out->data = vc->frame->data; */
/* 	    out->sizes = malloc(4 * sizeof(*out->sizes)); */
/* 	    for(i = 0; i < 4; i++){ */
/* 		out->sizes[i] = vc->frame->linesize[i]; */
/* 		if(out->sizes[i] == 0) */
/* 		    break; */
/* 	    } */
/* 	    out->planes = i; */
/* 	    if(vc->frame->pts) */
/* 		out->pts = vc->frame->pts; */
/* 	    else */
/* 		out->pts = vc->last_pts + vc->dpts; */
/* 	    vc->last_pts = out->pts; */
/* 	    out->free = avc_free_packet; */
/* 	    out->private = NULL; */
/* 	    vc->out->input(vc->out, out); */
/* 	} */

/* 	inbuf += l; */
/* 	insize -= l; */
/*     } */

/*     pk->free(pk); */

/*     return 0; */
/* } */

/* static int */
/* avc_free_adpipe(tcvp_pipe_t *p) */
/* { */
/*     avc_audio_codec_t *ac = p->private; */

/*     avcodec_close(ac->ctx); */
/*     if(ac->buf) */
/* 	free(ac->buf); */
/*     free(ac); */
/*     free(p); */

/*     return 0; */
/* } */

/* static int */
/* avc_free_vdpipe(tcvp_pipe_t *p) */
/* { */
/*     avc_video_codec_t *vc = p->private; */

/*     avcodec_close(vc->ctx); */
/*     free(vc->frame); */
/*     free(vc); */
/*     free(p); */

/*     return 0; */
/* } */

/* extern tcvp_pipe_t * */
/* avc_new(stream_t *s, int mode, tcvp_pipe_t *out) */
/* { */
/*     tcvp_pipe_t *p = NULL; */
/*     avc_audio_codec_t *ac; */
/*     avc_video_codec_t *vc; */
/*     AVCodec *avc = NULL; */
/*     AVCodecContext *avctx; */
/*     enum CodecID id; */

/*     id = avc_codec_id(s); */
/*     if(mode == CODEC_MODE_DECODE) */
/* 	avc = avcodec_find_decoder(id); */

/*     if(avc == NULL) */
/* 	return NULL; */

/*     avctx = avcodec_alloc_context(); */
/*     avcodec_get_context_defaults(avctx); */

/*     switch(s->stream_type){ */
/*     case STREAM_TYPE_AUDIO: */
/* 	ac = malloc(sizeof(*ac)); */
/* 	ac->ctx = avctx; */
/* 	ac->out = out; */
/* 	ac->buf = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE); */

/* 	p = malloc(sizeof(*p)); */
/* 	if(mode == CODEC_MODE_DECODE) */
/* 	    p->input = avc_decaudio; */
/* 	p->start = NULL; */
/* 	p->stop = NULL; */
/* 	p->free = avc_free_adpipe; */
/* 	p->private = ac; */
/* 	break; */

/*     case STREAM_TYPE_VIDEO: */
/* 	avctx->width = s->video.width; */
/* 	avctx->height = s->video.height; */
/* 	avctx->frame_rate = s->video.frame_rate * FRAME_RATE_BASE; */

/* 	vc = malloc(sizeof(*vc)); */
/* 	vc->ctx = avctx; */
/* 	vc->dpts = 1000000 / s->video.frame_rate; */
/* 	vc->last_pts = 0; */
/* 	vc->out = out; */
/* 	vc->frame = avcodec_alloc_frame(); */

/* 	p = malloc(sizeof(*p)); */
/* 	if(mode == CODEC_MODE_DECODE) */
/* 	    p->input = avc_decvideo; */
/* 	p->start = NULL; */
/* 	p->stop = NULL; */
/* 	p->free = avc_free_vdpipe; */
/* 	p->private = vc; */
/* 	break; */
/*     } */

/*     avcodec_open(avctx, avc); */

/*     return p; */
/* } */

/* static enum CodecID */
/* avc_codec_id(stream_t *s) */
/* { */
/*     int i; */
/*     char *n; */

/*     switch(s->stream_type){ */
/*     case STREAM_TYPE_VIDEO: */
/* 	n = s->video.codec; */
/* 	break; */
/*     case STREAM_TYPE_AUDIO: */
/* 	n = s->audio.codec; */
/* 	break; */
/*     default: */
/* 	return 0; */
/*     } */

/*     for(i = 0; codec_names[i]; i++){ */
/* 	if(!strcmp(n, codec_names[i])){ */
/* 	    return i; */
/* 	} */
/*     } */

/*     return 0; */
/* } */
