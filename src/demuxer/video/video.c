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
#include <tcvp.h>
#include <video_tc2.h>


extern muxed_stream_t *
v_open(char *name)
{
    video_open_t vopen = tc2_get_symbol("video/mpeg", "open");
    return vopen(name);
}

extern packet_t *
v_next_packet(muxed_stream_t *ms, int stream)
{
    return ms->next_packet(ms, stream);
}

extern int
v_close(muxed_stream_t *ms)
{
    return ms->close(ms);
}

#define RUN     1
#define PAUSE   2
#define STOP    3

typedef struct v_play {
    muxed_stream_t *stream;
    tcvp_pipe_t **pipes;
    pthread_t *threads;
    int state;
    pthread_mutex_t mtx;
    pthread_cond_t cnd;
} v_play_t;

typedef struct vp_thread {
    int stream;
    v_play_t *vp;
} vp_thread_t;

static void *
play_stream(void *p)
{
    vp_thread_t *vt = p;
    v_play_t *vp = vt->vp;
    muxed_stream_t *ms = vp->stream;
    int stream = vt->stream;
    packet_t *pk;

    fprintf(stderr, "playing stream %i\n", vt->stream);

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

    fprintf(stderr, "done playing stream %i\n", vt->stream);

    free(vt);
    return NULL;
}

static int
start(tcvp_pipe_t *p)
{
    v_play_t *vp = p->private;

    pthread_mutex_lock(&vp->mtx);
    vp->state = RUN;
    pthread_cond_broadcast(&vp->cnd);
    pthread_mutex_unlock(&vp->mtx);

    return 0;
}

static int
stop(tcvp_pipe_t *p)
{
    v_play_t *vp = p->private;

    pthread_mutex_lock(&vp->mtx);
    vp->state = PAUSE;
    pthread_mutex_unlock(&vp->mtx);

    return 0;
}

static int
v_free(tcvp_pipe_t *p)
{
    v_play_t *vp = p->private;
    int i, j;

    pthread_mutex_lock(&vp->mtx);
    vp->state = STOP;
    pthread_mutex_unlock(&vp->mtx);

    for(i = 0, j = 0; i < vp->stream->n_streams; i++){
	if(vp->stream->used_streams[i]){
	    pthread_join(vp->threads[j], NULL);
	    j++;
	}
    }

    free(vp->pipes);
    free(vp->threads);
    free(vp);
    free(p);

    return 0;
}

extern tcvp_pipe_t *
v_play(muxed_stream_t *ms, tcvp_pipe_t **out)
{
    tcvp_pipe_t *p;
    v_play_t *vp;
    int i, j;

    vp = malloc(sizeof(*vp));
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
	    vp->pipes[i] = out[j];
	    pthread_create(&vp->threads[j], NULL, play_stream, th);
	    j++;
	}
    }

    p = calloc(1, sizeof(tcvp_pipe_t));
    p->input = NULL;
    p->start = start;
    p->stop = stop;
    p->free = v_free;
    p->private = vp;

    return p;
}
