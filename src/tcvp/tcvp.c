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
#include <tcvp_tc2.h>

typedef struct tcvp_player {
    tcvp_pipe_t *demux, *vcodec, *acodec, *sound, *video;
    muxed_stream_t *stream;
    timer__t *timer;
    tcvp_status_cb_t status;
    void *cbdata;
    pthread_t th_wait, th_ticker;
    pthread_mutex_t tmx;
    int state;
} tcvp_player_t;

static int
t_start(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    if(tp->video)
	tp->video->start(tp->video);

    if(tp->sound)
	tp->sound->start(tp->sound);

    if(tp->timer)
	tp->timer->start(tp->timer);

    return 0;
}

static int
t_stop(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    if(tp->timer)
	tp->timer->stop(tp->timer);

    if(tp->sound)
	tp->sound->stop(tp->sound);

    if(tp->video)
	tp->video->stop(tp->video);

    return 0;
}

static int
t_close(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    if(tp->demux)
	tp->demux->free(tp->demux);

    if(tp->acodec)
	tp->acodec->free(tp->acodec);

    if(tp->vcodec)
	tp->vcodec->free(tp->vcodec);

    if(tp->video)
	tp->video->free(tp->video);

    if(tp->sound)
	tp->sound->free(tp->sound);

    if(tp->stream)
	tp->stream->close(tp->stream);

    pthread_mutex_lock(&tp->tmx);
    tp->state = TCVP_STATE_END;
    if(tp->timer)
	tp->timer->interrupt(tp->timer);
    pthread_mutex_unlock(&tp->tmx);

    pthread_join(tp->th_wait, NULL);

    if(tp->timer){
	pthread_join(tp->th_ticker, NULL);
	tp->timer->free(tp->timer);
    }

    free(tp);
    free(pl);

    return 0;
}

static void
print_info(muxed_stream_t *stream)
{
    int i;

    for(i = 0; i < stream->n_streams; i++){
	if(!stream->used_streams[i])
	    continue;

	printf("Stream %i, ", i);
	switch(stream->streams[i].stream_type){
	case STREAM_TYPE_AUDIO:
	    printf("%s, %i Hz, %i channels\n",
		   stream->streams[i].audio.codec,
		   stream->streams[i].audio.sample_rate,
		   stream->streams[i].audio.channels);
	    break;

	case STREAM_TYPE_VIDEO:
	    printf("%s, %ix%i, %lf fps\n",
		   stream->streams[i].video.codec,
		   stream->streams[i].video.width,
		   stream->streams[i].video.height,
		   (double) stream->streams[i].video.frame_rate.num / 
		   stream->streams[i].video.frame_rate.den);
	    break;
	}
    }
}

static void *
t_wait(void *p)
{
    tcvp_player_t *tp = p;

    tp->demux->flush(tp->demux, 0);
    pthread_mutex_lock(&tp->tmx);
    tp->state = TCVP_STATE_END;
    if(tp->timer)
	tp->timer->interrupt(tp->timer);
    pthread_mutex_unlock(&tp->tmx);

    return NULL;
}

static void *
st_ticker(void *p)
{
    tcvp_player_t *tp = p;
    uint64_t time = 0;

    pthread_mutex_lock(&tp->tmx);
    while(tp->state != TCVP_STATE_END){
	pthread_mutex_unlock(&tp->tmx);
	if(tp->timer->wait(tp->timer, time += 100000) == 0){
	    if(tp->status){
		tp->status(tp->cbdata, tp->state, time);
	    }
	}
	pthread_mutex_lock(&tp->tmx);
    }
    pthread_mutex_unlock(&tp->tmx);

    if(tp->status){
	tp->status(tp->cbdata, tp->state, tp->timer->read(tp->timer));
    }

    return NULL;
}


extern player_t *
t_open(char *name, tcvp_status_cb_t stcb, void *cbdata, conf_section *cs)
{
    int i;
    timer_new_t tmnew;
    stream_t *as = NULL, *vs = NULL;
    tcvp_pipe_t **codecs;
    tcvp_pipe_t *demux = NULL;
    tcvp_pipe_t *vcodec = NULL, *acodec = NULL;
    tcvp_pipe_t *sound = NULL, *video = NULL;
    muxed_stream_t *stream = NULL;
    timer__t *timer = NULL;
    tcvp_player_t *tp;
    player_t *pl;
    int ac, vc;

    if((stream = stream_open(name, cs)) == NULL)
	return NULL;

    codecs = alloca(stream->n_streams * sizeof(*codecs));

    for(i = 0; i < stream->n_streams; i++){
	stream_t *st = &stream->streams[i];
	if(stream->streams[i].stream_type == STREAM_TYPE_VIDEO && !vs){
	    if((vcodec = codec_new(st, CODEC_MODE_DECODE))){
		vs = st;
		codecs[i] = vcodec;
		stream->used_streams[i] = 1;
		vc = i;
	    }
	} else if(stream->streams[i].stream_type == STREAM_TYPE_AUDIO && !as){
	    if((acodec = codec_new(st, CODEC_MODE_DECODE))){
		as = &stream->streams[i];
		codecs[i] = acodec;
		stream->used_streams[i] = 1;
		ac = i;
	    }
	}
    }

    if(!as && !vs){
	printf("No supported streams found.\n");
	stream->close(stream);
	return NULL;
    }

    stream_probe(stream, codecs);

    print_info(stream);

    if(as){
	if((sound = output_audio_open(&as->audio, cs, &timer))){
	    acodec->next = sound;
	} else {
	    stream->used_streams[ac] = 0;
	    codecs[ac] = NULL;
	    acodec->free(acodec);
	    acodec = NULL;
	}
    }

    if(vs){
	if(!timer){
	    tmnew = tc2_get_symbol("timer", "new");
	    timer = tmnew(10000);
	}
	if((video = output_video_open(&vs->video, cs, timer))){
	    vcodec->next = video;
	} else {
	    stream->used_streams[vc] = 0;
	    codecs[vc] = NULL;
	    vcodec->free(vcodec);
	    vcodec = NULL;
	}
    }

    demux = stream_play(stream, codecs);

    tp = malloc(sizeof(*tp));
    tp->demux = demux;
    tp->vcodec = vcodec;
    tp->acodec = acodec;
    tp->sound = sound;
    tp->video = video;
    tp->stream = stream;
    tp->timer = timer;
    tp->status = stcb;
    tp->cbdata = cbdata;
    pthread_mutex_init(&tp->tmx, NULL);
    tp->state = TCVP_STATE_PLAYING;

    if(timer)
	pthread_create(&tp->th_ticker, NULL, st_ticker, tp);
    pthread_create(&tp->th_wait, NULL, t_wait, tp);

    pl = malloc(sizeof(*pl));
    pl->start = t_start;
    pl->stop = t_stop;
    pl->seek = NULL;
    pl->close = t_close;
    pl->private = tp;

    demux->start(demux);

    return pl;
}
