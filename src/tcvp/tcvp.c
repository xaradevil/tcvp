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
    pthread_t th_ticker, th_event, th_wait;
    pthread_mutex_t tmx;
    pthread_cond_t tcd;
    int state;
    eventq_t qs, qr;
    conf_section *conf;
    int open;
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

    tp->state = TCVP_STATE_PLAYING;
    tcvp_state_event_t *te = tcvp_alloc_event();
    te->type = TCVP_STATE;
    te->state = TCVP_STATE_PLAYING;
    eventq_send(tp->qs, te);
    tcfree(te);

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

    tp->state = TCVP_STATE_STOPPED;
    tcvp_state_event_t *te = tcvp_alloc_event();
    te->type = TCVP_STATE;
    te->state = TCVP_STATE_STOPPED;
    eventq_send(tp->qs, te);
    tcfree(te);

    return 0;
}

static int
t_close(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    if(tp->demux){
	tp->demux->flush(tp->demux, 1);
	pthread_join(tp->th_wait, NULL);
	tp->demux->free(tp->demux);
	tp->demux = NULL;
    }

    if(tp->acodec){
	tp->acodec->free(tp->acodec);
	tp->acodec = NULL;
    }

    if(tp->vcodec){
	tp->vcodec->free(tp->vcodec);
	tp->vcodec = NULL;
    }

    if(tp->video){
	tp->video->free(tp->video);
	tp->video = NULL;
    }

    if(tp->sound){
	tp->sound->free(tp->sound);
	tp->sound = NULL;
    }

    if(tp->stream){
	tp->stream->close(tp->stream);
	tp->stream = NULL;
    }

    pthread_mutex_lock(&tp->tmx);
    tp->state = TCVP_STATE_END;
    if(tp->timer)
	tp->timer->interrupt(tp->timer);
    pthread_mutex_unlock(&tp->tmx);

    pthread_join(tp->th_ticker, NULL);
    if(tp->timer){
	tp->timer->free(tp->timer);
	tp->timer = NULL;
    }

    pthread_mutex_lock(&tp->tmx);
    tp->open = 0;
    pthread_cond_broadcast(&tp->tcd);
    pthread_mutex_unlock(&tp->tmx);

    return 0;
}

static void
t_free(player_t *pl)
{
    tcvp_player_t *tp = pl->private;
    tcvp_event_t *te;

    pthread_mutex_lock(&tp->tmx);
    while(tp->open)
	pthread_cond_wait(&tp->tcd, &tp->tmx);
    pthread_mutex_unlock(&tp->tmx);

    te = tcvp_alloc_event();
    te->type = -1;
    eventq_send(tp->qr, te);
    tcfree(te);
    pthread_join(tp->th_event, NULL);

    eventq_delete(tp->qr);
    eventq_delete(tp->qs);
    pthread_mutex_destroy(&tp->tmx);
    pthread_cond_destroy(&tp->tcd);

    free(tp);
    free(pl);
}

static void
print_info(muxed_stream_t *stream)
{
    int i;

    for(i = 0; i < stream->n_streams; i++){
	printf("Stream %i%s, ", i, stream->used_streams[i]? "*": "");
	switch(stream->streams[i].stream_type){
	case STREAM_TYPE_AUDIO:
	    printf("%s, %i Hz, %i channels, %i kb/s\n",
		   stream->streams[i].audio.codec,
		   stream->streams[i].audio.sample_rate,
		   stream->streams[i].audio.channels,
		   stream->streams[i].audio.bit_rate / 1000);
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

    tcvp_state_event_t *te = tcvp_alloc_event();
    te->type = TCVP_STATE;
    te->state = TCVP_STATE_END;
    eventq_send(tp->qs, te);
    tcfree(te);

    return NULL;
}

static void *
st_ticker(void *p)
{
    tcvp_player_t *tp = p;
    uint64_t time = 0;
    eventq_t qt;

    qt = eventq_new(NULL);
    eventq_attach(qt, "TCVP/timer", EVENTQ_SEND);

    pthread_mutex_lock(&tp->tmx);
    while(tp->state != TCVP_STATE_END){
	pthread_mutex_unlock(&tp->tmx);
	if(tp->timer->wait(tp->timer, time += 1000000) == 0){
	    tcvp_timer_event_t *te = tcvp_alloc_event();
	    te->type = TCVP_TIMER;
	    te->time = time;
	    eventq_send(qt, te);
	    tcfree(te);
	}
	pthread_mutex_lock(&tp->tmx);
    }
    pthread_mutex_unlock(&tp->tmx);

    eventq_delete(qt);
    return NULL;
}

static int
t_seek(player_t *pl, int64_t time, int how)
{
    tcvp_player_t *tp = pl->private;
    uint64_t ntime;

    if(tp->stream->seek){
	int s = tp->state;
	if(s == TCVP_STATE_PLAYING){
	    tp->demux->stop(tp->demux);
	    t_stop(pl);
	}

	if(how == TCVP_SEEK_REL){
	    ntime = tp->timer->read(tp->timer);
	    if(time < 0 && -time > ntime)
		ntime = 0;
	    else
		ntime += time;
	} else {
	    ntime = time;
	}
	ntime = tp->stream->seek(tp->stream, ntime);
	if(ntime != -1LL){
	    tp->timer->reset(tp->timer, ntime);
	    if(tp->vcodec)
		tp->vcodec->flush(tp->vcodec, 1);
	    if(tp->acodec)
		tp->acodec->flush(tp->acodec, 1);
	}
	if(s == TCVP_STATE_PLAYING){
	    tp->demux->start(tp->demux);
	    t_start(pl);
	}
    }

    return 0;    
}

static int
t_open(player_t *pl, char *name)
{
    int i;
    stream_t *as = NULL, *vs = NULL;
    tcvp_pipe_t **codecs;
    tcvp_pipe_t *demux = NULL;
    tcvp_pipe_t *vcodec = NULL, *acodec = NULL;
    tcvp_pipe_t *sound = NULL, *video = NULL;
    muxed_stream_t *stream = NULL;
    timer__t *timer = NULL;
    tcvp_player_t *tp = pl->private;
    int ac = -1, vc = -1;
    int start;

    if((stream = stream_open(name, tp->conf)) == NULL){
	return -1;
    }

    codecs = calloc(stream->n_streams, sizeof(*codecs));

    if(tp->conf){
	if(conf_getvalue(tp->conf, "video/stream", "%i", &vc) > 0){
	    if(vc >= 0 && vc < stream->n_streams &&
	       stream->streams[vc].stream_type == STREAM_TYPE_VIDEO){
		vs = &stream->streams[vc];
	    } else {
		vc = -2;
	    }
	}
	if(conf_getvalue(tp->conf, "audio/stream", "%i", &ac) > 0){
	    if(ac >= 0 && ac < stream->n_streams &&
	       stream->streams[ac].stream_type == STREAM_TYPE_AUDIO){
		as = &stream->streams[ac];
	    } else {
		ac = -2;
	    }
	}
    }

    for(i = 0; i < stream->n_streams; i++){
	stream_t *st = &stream->streams[i];
	if(stream->streams[i].stream_type == STREAM_TYPE_VIDEO &&
	   (!vs || i == vc) && vc > -2){
	    if((vcodec = codec_new(st, CODEC_MODE_DECODE))){
		vs = st;
		codecs[i] = vcodec;
		stream->used_streams[i] = 1;
		vc = i;
	    } else if(vs){
		printf("Warning: Stream %i not supported => no video\n", i);
		vs = NULL;
	    }
	} else if(stream->streams[i].stream_type == STREAM_TYPE_AUDIO &&
		  (!as || i == ac) && ac > -2){
	    if((acodec = codec_new(st, CODEC_MODE_DECODE))){
		as = &stream->streams[i];
		codecs[i] = acodec;
		stream->used_streams[i] = 1;
		ac = i;
	    } else if(as){
		printf("Warning: Stream %i not supported => no audio\n", i);
		as = NULL;
	    }
	}
    }

    if(!as && !vs){
	printf("No supported streams found.\n");
	stream->close(stream);
	return -1;
    }

    tp->open = 1;

    stream_probe(stream, codecs);

    print_info(stream);

    if(as){
	if((sound = output_audio_open(&as->audio, tp->conf, &timer))){
	    acodec->next = sound;
	} else {
	    stream->used_streams[ac] = 0;
	    codecs[ac] = NULL;
	    acodec->free(acodec);
	    acodec = NULL;
	}
    }

    if(!timer){
	timer = timer_new(10000);
    }

    if(vs){
	if((video = output_video_open(&vs->video, tp->conf, timer))){
	    vcodec->next = video;
	} else {
	    stream->used_streams[vc] = 0;
	    codecs[vc] = NULL;
	    vcodec->free(vcodec);
	    vcodec = NULL;
	}
    }

    demux = stream_play(stream, codecs);

    tp->state = TCVP_STATE_STOPPED;
    tp->demux = demux;
    tp->vcodec = vcodec;
    tp->acodec = acodec;
    tp->sound = sound;
    tp->video = video;
    tp->stream = stream;
    tp->timer = timer;

    if(conf_getvalue(tp->conf, "start_time", "%i", &start) == 1){
	uint64_t spts = (uint64_t) start * 1000000LL;
	t_seek(pl, spts, TCVP_SEEK_ABS);
    }

    pthread_create(&tp->th_ticker, NULL, st_ticker, tp);
    pthread_create(&tp->th_wait, NULL, t_wait, tp);

    demux->start(demux);
    if(tp->video && tp->video->buffer)
	tp->video->buffer(tp->video, 0.9);
    if(tp->sound && tp->sound->buffer)
	tp->sound->buffer(tp->sound, 0.9);

    free(codecs);
    return 0;
}

static void *
t_event(void *p)
{
    player_t *pl = p;
    tcvp_player_t *tp = pl->private;
    int r = 1;

    while(r){
	tcvp_event_t *te = eventq_recv(tp->qr);
	switch(te->type){
	case TCVP_OPEN:
	    if(t_open(pl, te->open.file) < 0){
		tcvp_state_event_t *te = tcvp_alloc_event();
		te->type = TCVP_STATE;
		te->state = TCVP_STATE_ERROR;
		eventq_send(tp->qs, te);
		tcfree(te);
	    }		
	    break;

	case TCVP_START:
	    t_start(pl);
	    break;

	case TCVP_STOP:
	    t_stop(pl);
	    break;

	case TCVP_PAUSE:
	    if(tp->state == TCVP_STATE_PLAYING)
		t_stop(pl);
	    else if(tp->state == TCVP_STATE_STOPPED)
		t_start(pl);
	    break;

	case TCVP_SEEK:
	    t_seek(pl, te->seek.time, te->seek.how);
	    break;

	case TCVP_CLOSE:
	    t_close(pl);
	    break;

	case -1:
	    r = 0;
	    break;
	}
	tcfree(te);
    }
    return NULL;
}

static int
q_cmd(player_t *pl, int cmd)
{
    tcvp_player_t *tp = pl->private;
    tcvp_event_t *te = tcvp_alloc_event();
    te->type = cmd;
    eventq_send(tp->qr, te);
    tcfree(te);
    return 0;
}

static int
q_start(player_t *pl)
{
    return q_cmd(pl, TCVP_START);
}

static int
q_stop(player_t *pl)
{
    return q_cmd(pl, TCVP_STOP);
}

static int
q_close(player_t *pl)
{
    return q_cmd(pl, TCVP_CLOSE);
}

static int
q_seek(player_t *pl, uint64_t pts)
{
    tcvp_player_t *tp = pl->private;
    tcvp_seek_event_t *se = tcvp_alloc_event();
    se->type = TCVP_SEEK;
    se->time = pts;
    eventq_send(tp->qr, se);
    tcfree(se);
    return 0;
}

extern player_t *
t_new(conf_section *cs)
{
    tcvp_player_t *tp;
    player_t *pl;

    tp = calloc(1, sizeof(*tp));
    pl = malloc(sizeof(*pl));
    pl->start = q_start;
    pl->stop = q_stop;
    pl->seek = q_seek;
    pl->close = q_close;
    pl->free = t_free;
    pl->private = tp;

    pthread_mutex_init(&tp->tmx, NULL);
    pthread_cond_init(&tp->tcd, NULL);
    tp->qs = eventq_new(NULL);
    eventq_attach(tp->qs, "TCVP/status", EVENTQ_SEND);
    tp->qr = eventq_new(tcref);
    eventq_attach(tp->qr, "TCVP/control", EVENTQ_RECV);
    pthread_create(&tp->th_event, NULL, t_event, pl);
    tp->conf = cs;

    return pl;
}
