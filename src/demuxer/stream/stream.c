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
#include <pthread.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <stream_tc2.h>


extern muxed_stream_t *
s_open(char *name, conf_section *cs)
{
    char *m = NULL;
    char *ext;

    ext = strrchr(name, '.');

    if(ext++){
	if(!strcmp(ext, "ogg")) {
	    m = "audio/ogg";
	} else if(!strcmp(ext, "avi")){
	    m = "video/x-avi";
	} else if(!strcmp(ext, "mp3")){
	    m = "audio/mp3";
	}
    }

    if(!m){
	m = "video/mpeg";
    }

    stream_open_t sopen = tc2_get_symbol(m, "open");
    return sopen(name, cs);
}

extern packet_t *
s_next_packet(muxed_stream_t *ms, int stream)
{
    return ms->next_packet(ms, stream);
}

extern int
s_validate(char *name, conf_section *cs)
{
    muxed_stream_t *ms = s_open(name, cs);

    if(!ms)
	return -1;

    while(ms->next_packet(ms, -1));

    tcfree(ms);
    return 0;
}

#define RUN     1
#define PAUSE   2
#define STOP    3

typedef struct s_play {
    muxed_stream_t *stream;
    tcvp_pipe_t **pipes;
    int streams;
    pthread_t *threads;
    int state;
    int flushing;
    pthread_mutex_t mtx;
    pthread_cond_t cnd;
} s_play_t;

typedef struct vp_thread {
    int stream;
    s_play_t *vp;
} vp_thread_t;

static void *
play_stream(void *p)
{
    vp_thread_t *vt = p;
    s_play_t *vp = vt->vp;
    muxed_stream_t *ms = vp->stream;
    int stream = vt->stream;
    packet_t *pk;

    while(vp->state != STOP){
	pthread_mutex_lock(&vp->mtx);
	while(vp->state == PAUSE){
	    pthread_cond_wait(&vp->cnd, &vp->mtx);
	}
	pthread_mutex_unlock(&vp->mtx);

	if(!(pk = ms->next_packet(ms, stream)))
	    break;

	vp->pipes[stream]->input(vp->pipes[stream], pk);
    }

    vp->pipes[stream]->input(vp->pipes[stream], NULL);
    vp->pipes[stream]->flush(vp->pipes[stream], vp->state == STOP);

    pthread_mutex_lock(&vp->mtx);
    vp->streams--;
    pthread_cond_broadcast(&vp->cnd);
    pthread_mutex_unlock(&vp->mtx);

    free(vt);
    return NULL;
}

static int
start(tcvp_pipe_t *p)
{
    s_play_t *vp = p->private;

    pthread_mutex_lock(&vp->mtx);
    vp->state = RUN;
    pthread_cond_broadcast(&vp->cnd);
    pthread_mutex_unlock(&vp->mtx);

    return 0;
}

static int
stop(tcvp_pipe_t *p)
{
    s_play_t *vp = p->private;

    vp->state = PAUSE;

    return 0;
}

static void
flush_all(s_play_t *vp, int drop)
{
    int i;

    for(i = 0; i < vp->stream->n_streams; i++){
	if(vp->stream->used_streams[i]){
	    vp->pipes[i]->flush(vp->pipes[i], drop);
	}
    }
}

static int
s_flush(tcvp_pipe_t *p, int drop)
{
    s_play_t *vp = p->private;

    pthread_mutex_lock(&vp->mtx);
    vp->flushing++;
    pthread_mutex_unlock(&vp->mtx);

    if(drop){
	vp->state = STOP;
	flush_all(vp, drop);
    }

    pthread_mutex_lock(&vp->mtx);
    while(vp->streams > 0)
	pthread_cond_wait(&vp->cnd, &vp->mtx);
    pthread_mutex_unlock(&vp->mtx);

    if(!drop)
	flush_all(vp, drop);

    pthread_mutex_lock(&vp->mtx);
    if(--vp->flushing == 0)
	pthread_cond_broadcast(&vp->cnd);
    pthread_mutex_unlock(&vp->mtx);

    return 0;
}

static int
s_free(tcvp_pipe_t *p)
{
    s_play_t *vp = p->private;
    int i, j;

    s_flush(p, 1);

    for(i = 0, j = 0; i < vp->stream->n_streams; i++){
	if(vp->stream->used_streams[i]){
	    pthread_join(vp->threads[j], NULL);
	    j++;
	}
    }

    pthread_mutex_lock(&vp->mtx);
    while(vp->flushing)
	pthread_cond_wait(&vp->cnd, &vp->mtx);
    pthread_mutex_unlock(&vp->mtx);

    free(vp->pipes);
    free(vp->threads);
    free(vp);
    free(p);

    return 0;
}

extern tcvp_pipe_t *
s_play(muxed_stream_t *ms, tcvp_pipe_t **out)
{
    tcvp_pipe_t *p;
    s_play_t *vp;
    int i, j;

    vp = calloc(1, sizeof(*vp));
    vp->stream = ms;
    vp->pipes = calloc(ms->n_streams, sizeof(tcvp_pipe_t *));
    vp->threads = calloc(ms->n_streams, sizeof(pthread_t));
    vp->state = PAUSE;
    pthread_mutex_init(&vp->mtx, NULL);
    pthread_cond_init(&vp->cnd, NULL);

    for(i = 0, j = 0; i < ms->n_streams; i++){
	if(ms->used_streams[i]){
	    vp_thread_t *th = malloc(sizeof(*th));
	    th->stream = i;
	    th->vp = vp;
	    vp->pipes[i] = out[i];
	    pthread_create(&vp->threads[j], NULL, play_stream, th);
	    j++;
	}
    }

    vp->streams = j;

    p = calloc(1, sizeof(tcvp_pipe_t));
    p->input = NULL;
    p->start = start;
    p->stop = stop;
    p->free = s_free;
    p->flush = s_flush;
    p->private = vp;

    return p;
}

extern int
s_probe(muxed_stream_t *ms, tcvp_pipe_t **codecs)
{
    int i;

    for(i = 0; i < ms->n_streams; i++){
	if(ms->used_streams[i] && codecs[i]->probe){
	    int p;
	    do {
		packet_t *pk = ms->next_packet(ms, i);
		if(!pk)
		    break;
		p = codecs[i]->probe(codecs[i], pk, &ms->streams[i]);
	    } while(p == PROBE_AGAIN);
	}
    }

    return PROBE_OK;
}
