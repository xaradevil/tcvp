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
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <semaphore.h>
#include <tcvp_types.h>
#include <video_tc2.h>

#define PLAY  1
#define PAUSE 2
#define STOP  3

#undef DEBUG

typedef struct video_out {
    video_driver_t *driver;
    timer__t *timer;
    uint64_t *pts;
    int state;
    pthread_mutex_t smx;
    pthread_cond_t scd;
    pthread_t thr;
    int head, tail;
    sem_t hsem, tsem;
} video_out_t;

static void *
v_play(void *p)
{
    video_out_t *vo = p;

#ifdef DEBUG
    int f = 0;
    struct timeval tv;
    uint64_t lu = 0, st;
    uint64_t lpts = 0;

    gettimeofday(&tv, NULL);
    st = tv.tv_sec * 1000000 + tv.tv_usec;
#endif

    while(vo->state != STOP){
	pthread_mutex_lock(&vo->smx);
	while(vo->state == PAUSE){
	    pthread_cond_wait(&vo->scd, &vo->smx);
	}
	pthread_mutex_unlock(&vo->smx);

	sem_wait(&vo->tsem);
	if(vo->state == STOP)
	    break;

	if(vo->timer->wait(vo->timer, vo->pts[vo->tail]) < 0)
	    break;

#ifdef DEBUG
	if(!(++f & 0x3f)){
	    uint64_t us;
	    gettimeofday(&tv, NULL);
	    us = tv.tv_sec * 1000000 + tv.tv_usec - st;

	    fprintf(stderr, "pts = %li, dpts = %li, us = %li, dus = %li\n",
		    vo->pts[vo->tail],
		    vo->pts[vo->tail] - lpts,
		    us, us - lu);
	    lu = us;
	    lpts = vo->pts[vo->tail];
	}
#endif

	vo->driver->show_frame(vo->driver, vo->tail);

	sem_post(&vo->hsem);

	if(++vo->tail == vo->driver->frames){
	    vo->tail = 0;
	}
    }

    return NULL;
}

static int
v_put(tcvp_pipe_t *p, packet_t *pk)
{
    video_out_t *vo = p->private;

    sem_wait(&vo->hsem);

    vo->driver->put_frame(vo->driver, pk, vo->head);
    vo->pts[vo->head] = pk->pts;

    sem_post(&vo->tsem);

    if(++vo->head == vo->driver->frames){
	vo->head = 0;
    }

    return 0;
}

static int
v_start(tcvp_pipe_t *p)
{
    video_out_t *vo = p->private;

    pthread_mutex_lock(&vo->smx);
    vo->state = PLAY;
    pthread_cond_broadcast(&vo->scd);
    pthread_mutex_unlock(&vo->smx);

    return 0;
}

static int
v_stop(tcvp_pipe_t *p)
{
    video_out_t *vo = p->private;

    vo->state = PAUSE;

    return 0;
}

static int
v_flush(tcvp_pipe_t *p, int drop)
{
    video_out_t *vo = p->private;

    if(!drop){
	while(vo->head != vo->tail){
	    sem_wait(&vo->hsem);
	}
    } else {
	while(sem_trywait(&vo->tsem) == 0){
	    sem_post(&vo->hsem);
	}
	vo->tail = vo->head;
    }

    return 0;
}

static int
v_free(tcvp_pipe_t *p)
{
    video_out_t *vo = p->private;

    vo->state = STOP;
    sem_post(&vo->tsem);
    vo->timer->interrupt(vo->timer);
    pthread_join(vo->thr, NULL);

    vo->driver->close(vo->driver);

    pthread_mutex_destroy(&vo->smx);
    pthread_cond_destroy(&vo->scd);
    sem_destroy(&vo->hsem);
    sem_destroy(&vo->tsem);

    free(vo->pts);
    free(vo);
    free(p);

    return 0;
}

extern tcvp_pipe_t *
v_open(video_stream_t *vs, char *device, timer__t *timer)
{
    tcvp_pipe_t *pipe;
    video_out_t *vo;
    video_driver_t *vd = NULL;
    int i;

    for(i = 0; i < output_video_conf_driver_count; i++){
	driver_video_open_t vdo;
	char buf[256];

	sprintf(buf, "driver/video/%s", output_video_conf_driver[i].name);
	if(!(vdo = tc2_get_symbol(buf, "open")))
	    continue;

	if((vd = vdo(vs, device))){
	    break;
	}
    }

    if(!vd)
	return NULL;

    vo = malloc(sizeof(*vo));
    vo->driver = vd;
    vo->timer = timer;
    vo->pts = malloc(vd->frames * sizeof(*vo->pts));
    vo->head = 0;
    vo->tail = 0;
    sem_init(&vo->hsem, 0, vd->frames);
    sem_init(&vo->tsem, 0, 0);
    pthread_mutex_init(&vo->smx, NULL);
    pthread_cond_init(&vo->scd, NULL);
    vo->state = PAUSE;
    pthread_create(&vo->thr, NULL, v_play, vo);

    pipe = malloc(sizeof(*pipe));
    pipe->input = v_put;
    pipe->start = v_start;
    pipe->stop = v_stop;
    pipe->free = v_free;
    pipe->flush = v_flush;
    pipe->private = vo;

    return pipe;
}

static int
drv_cmp(const void *p1, const void *p2)
{
    const output_video_conf_driver_t *d1 = p1, *d2 = p2;
    return d2->priority - d1->priority;
}

extern int
v_init(char *arg)
{
    if(output_video_conf_driver_count > 0){
	qsort(output_video_conf_driver, output_video_conf_driver_count,
	      sizeof(output_video_conf_driver_t), drv_cmp);
    }

    return 0;
}
