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
#include <ffmpeg/avformat.h>
#include <avf.h>
#include <avformat_tc2.h>

extern int
avf_init(char *p)
{
    av_register_all();
    return 0;
}

static char *codec_names[] = {
    [CODEC_ID_NONE] = NULL, 
    [CODEC_ID_MPEG1VIDEO] = "video/mpeg",
    [CODEC_ID_MPEG2VIDEO] = "video/mpeg2",
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
    [CODEC_ID_ADPCM_MS] = "audio/adpcm-ms"
};

extern enum CodecID
avf_codec_id(char *codec)
{
    int i;

    for(i = 0; i < sizeof(codec_names)/sizeof(codec_names[0]); i++){
	if(codec_names[i] && !strcmp(codec, codec_names[i])){
	    return i;
	}
    }

    return 0;
}

typedef struct {
    AVFormatContext *afc;
} avf_stream_t;


static void
avf_free_packet(void *v)
{
    packet_t *p = v;
    AVPacket *ap = p->private;
    av_free_packet(ap);
    free(ap);
    free(p->sizes);
}

extern packet_t *
avf_next_packet(muxed_stream_t *ms, int stream)
{
    avf_stream_t *as = ms->private;
    AVFormatContext *afc = as->afc;
    AVPacket *apk = NULL;
    packet_t *pk;
    int sx;

    apk = calloc(1, sizeof(*apk));

    do {
	if(av_read_packet(afc, apk) < 0){
	    pk = NULL;
	    return NULL;
	}

	sx = apk->stream_index;

	if(!ms->used_streams[sx])
	    av_free_packet(apk);
    } while(!ms->used_streams[sx]);

    pk = tcallocd(sizeof(*pk), NULL, avf_free_packet);
    pk->stream = sx;
    pk->data = &apk->data;
    pk->sizes = malloc(sizeof(*pk->sizes));
    pk->sizes[0] = apk->size;
    pk->planes = 1;
    pk->flags = 0;
    pk->pts = 0;
    pk->private = apk;

    if(!pk)
	free(apk);

    return pk;
}

extern void
avf_free(void *p)
{
    muxed_stream_t *ms = p;
    avf_stream_t *as = ms->private;

    av_close_input_file(as->afc);

    free(ms->streams);
    free(ms->used_streams);

    free(as);
}


extern muxed_stream_t *
avf_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    AVFormatContext *afc;
    muxed_stream_t *ms;
    avf_stream_t *as;
    int i;

    u->close(u);

    if(av_open_input_file(&afc, name, NULL, 0, NULL) != 0){
	fprintf(stderr, "Error opening %s\n", name);
	return NULL;
    }

    if(av_find_stream_info(afc) < 0){
	fprintf(stderr, "Can't find stream info for %s\n", name);
	return NULL;
    }

    ms = tcallocd(sizeof(*ms), NULL, avf_free);
    memset(ms, 0, sizeof(*ms));
    ms->n_streams = afc->nb_streams;
    ms->streams = calloc(ms->n_streams, sizeof(*ms->streams));
    for(i = 0; i < ms->n_streams; i++){
	switch(afc->streams[i]->codec.codec_type){
	case CODEC_TYPE_VIDEO:
	    ms->streams[i].stream_type = STREAM_TYPE_VIDEO;
	    ms->streams[i].video.frame_rate.num =
		afc->streams[i]->codec.frame_rate;
	    ms->streams[i].video.frame_rate.den =
		afc->streams[i]->codec.frame_rate_base;
	    ms->streams[i].video.width = afc->streams[i]->codec.width;
	    ms->streams[i].video.height = afc->streams[i]->codec.height;
	    ms->streams[i].video.codec =
		codec_names[afc->streams[i]->codec.codec_id];
	    break;

	case CODEC_TYPE_AUDIO:
	    ms->streams[i].stream_type = STREAM_TYPE_AUDIO;
	    ms->streams[i].audio.sample_rate =
		afc->streams[i]->codec.sample_rate;
	    ms->streams[i].audio.channels = afc->streams[i]->codec.channels;
	    ms->streams[i].audio.codec =
		codec_names[afc->streams[i]->codec.codec_id];
	    ms->streams[i].audio.bit_rate = afc->streams[i]->codec.bit_rate;
	    break;

	default:
	    ms->streams[i].stream_type = 0;
	    break;
	}

	ms->streams[i].common.codec_data = afc->streams[i]->codec.extradata;
	ms->streams[i].common.codec_data_size =
	    afc->streams[i]->codec.extradata_size;
	ms->streams[i].common.index = i;
    }
    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    ms->next_packet = avf_next_packet;

    as = calloc(1, sizeof(*as));
    as->afc = afc;

    ms->private = as;

    return ms;
}
