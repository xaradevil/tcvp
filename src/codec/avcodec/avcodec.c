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
    avcodec_close(vc->ctx);
    if(vc->buf)
	free(vc->buf);
    if(vc->frame)
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
    enum CodecID id;

    id = avc_codec_id(s->common.codec);
    if(id == CODEC_ID_MPEG1VIDEO)
	id = CODEC_ID_MPEG2VIDEO;

    avc = avcodec_find_decoder(id);

    if(avc == NULL){
	tc2_print("AVCODEC", TC2_PRINT_ERROR,
		  "Can't find codec for '%s' => %i\n",
		  s->common.codec, id);
	return -1;
    }

    avctx = avcodec_alloc_context();
    if((avc->capabilities & CODEC_CAP_TRUNCATED) &&
       (s->common.flags & TCVP_STREAM_FLAG_TRUNCATED)){
	tc2_print("AVCODEC", TC2_PRINT_DEBUG, "setting truncated flag\n");
	avctx->flags |= CODEC_FLAG_TRUNCATED;
    }

#define ctx_conf(n, f) tcconf_getvalue(cs, #n, "%"#f, &avctx->n)
    ctx_conf(workaround_bugs, i);
    ctx_conf(error_resilience, i);
    ctx_conf(error_concealment, i);
    ctx_conf(idct_algo, i);
    ctx_conf(debug, i);

    ac = tcallocdz(sizeof(*ac), NULL, avc_free_pipe);
    ac->ctx = avctx;
    p->private = ac;

    switch(s->stream_type){
    case STREAM_TYPE_AUDIO:
	avctx->sample_rate = s->audio.sample_rate;
	avctx->channels = s->audio.channels;
	avctx->bit_rate = s->audio.bit_rate;
	avctx->block_align = s->audio.block_align;
	ac->buf = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	p->format.common.codec = "audio/pcm-s16" TCVP_ENDIAN;
	break;

    case STREAM_TYPE_VIDEO:
	avctx->width = s->video.width;
	avctx->height = s->video.height;
	avctx->frame_rate = s->video.frame_rate.num;
	avctx->frame_rate_base = s->video.frame_rate.den;

	ac->ptsn = (uint64_t) 27000000 * s->video.frame_rate.den;
	ac->ptsd = s->video.frame_rate.num?: 1;
	ac->pts = 0;
	ac->frame = avcodec_alloc_frame();

	p->format.common.codec = "video/raw-i420";
	break;

    default:
	tc2_print("AVCODEC", TC2_PRINT_ERROR,
		  "unknown stream type %i\n", s->stream_type);
	return -1;
    }

    avctx->extradata = s->common.codec_data;
    avctx->extradata_size = s->common.codec_data_size;

    avcodec_open(avctx, avc);

    return 0;
}

static char *codec_names[][2] = {
    { (char *) CODEC_ID_NONE, "" }, 
    { (char *) CODEC_ID_MPEG1VIDEO, "video/mpeg" },
    { (char *) CODEC_ID_MPEG2VIDEO, "video/mpeg2" },
    { (char *) CODEC_ID_H263, "video/h263" },
    { (char *) CODEC_ID_RV10, "video/rv10" },
    { (char *) CODEC_ID_MP2, "audio/mp2" },
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
    { (char *) CODEC_ID_CINEPAK, "video/cinepak" },
    { (char *) CODEC_ID_TRUEMOTION1, "video/truemotion1" },

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
