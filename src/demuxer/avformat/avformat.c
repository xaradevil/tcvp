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
#include <tclist.h>
#include <pthread.h>
#include <avformat.h>
#include <avformat_tc2.h>


extern int
avf_init(char *p)
{
    av_register_all();
    return 0;
}

extern int
avf_shutdown(void)
{
    return 0;
}


static char *codec_names[] = {
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
    [CODEC_ID_ADPCM_MS] = "audio/adpcm-ms"
};


typedef struct {
    AVFormatContext *afc;
    list **packets;
    pthread_mutex_t mtx;
} avf_stream_t;


static void
avf_free_packet(packet_t *p)
{
    AVPacket *ap = p->private;
    av_free_packet(ap);
    free(p->sizes);
    free(p);
}

extern packet_t *
avf_next_packet(muxed_stream_t *ms, int stream)
{
    avf_stream_t *as = ms->private;
    AVFormatContext *afc = as->afc;
    AVPacket *apk;
    packet_t *pk = NULL;

    if((pk = list_shift(as->packets[stream])))
	return pk;

    apk = calloc(1, sizeof(*apk));

    do {
	int s;

	pthread_mutex_lock(&as->mtx);
	s = av_read_packet(afc, apk);
	pthread_mutex_unlock(&as->mtx);

	if(s < 0){
	    pk = NULL;
	    break;
	}

	pk = malloc(sizeof(*pk));
	pk->data = &apk->data;
	pk->sizes = malloc(sizeof(size_t));
	pk->sizes[0] = apk->size;
	pk->planes = 1;
	pk->pts = apk->pts;
	pk->free = avf_free_packet;
	pk->private = apk;

	if(apk->stream_index != stream && ms->used_streams[apk->stream_index])
	    list_push(as->packets[apk->stream_index], pk);
    } while(apk->stream_index != stream);

    if(!pk)
	free(apk);

    return pk;
}


extern int
avf_close(muxed_stream_t *ms)
{
    avf_stream_t *as = ms->private;
    int i;

    av_close_input_file(as->afc);

    free(ms->streams);
    free(ms->used_streams);

    for(i = 0; i < ms->n_streams; i++){
	list_destroy(as->packets[i], (tc_free_fn) avf_free_packet);
    }
    free(as->packets);
    free(as);
}


extern muxed_stream_t *
avf_open(char *name)
{
    AVFormatContext *afc;
    muxed_stream_t *ms;
    avf_stream_t *as;
    int i;

    if(av_open_input_file(&afc, name, NULL, 0, NULL) != 0)
	return NULL;

    if(av_find_stream_info(afc) != 0)
	return NULL;

    ms = malloc(sizeof(*ms));
    ms->n_streams = afc->nb_streams;
    ms->streams = malloc(ms->n_streams * sizeof(stream_t));
    for(i = 0; i < ms->n_streams; i++){
	switch(afc->streams[i]->codec.codec_type){
	case CODEC_TYPE_VIDEO:
	    ms->streams[i].stream_type = STREAM_TYPE_VIDEO;
	    ms->streams[i].video.frame_rate =
		(float) afc->streams[i]->codec.frame_rate / FRAME_RATE_BASE;
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
	    break;
	}
    }
    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    ms->next_packet = avf_next_packet;
    ms->close = avf_close;

    as = malloc(sizeof(*as));
    as->afc = afc;
    as->packets = calloc(ms->n_streams, sizeof(list *));
    for(i = 0; i < ms->n_streams; i++){
	as->packets[i] = list_new(TC_LOCK_SLOPPY);
    }
    pthread_mutex_init(&as->mtx, NULL);

    ms->private = as;

    return ms;
}
