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
#include <vid_priv.h>

#define PLAY  1
#define PAUSE 2
#define STOP  3

#undef DEBUG

typedef struct video_out {
    video_driver_t *driver;
    video_stream_t *vstream;
    timer__t *timer;
    color_conv_t cconv;
    uint64_t *pts;
    int state;
    pthread_mutex_t smx;
    pthread_cond_t scd;
    pthread_t thr;
    int head, tail;
    int frames;
    int *drop;
    int dropcnt;
    int framecnt;
} video_out_t;

#define DROPLEN 8

static int drops[][DROPLEN] = {
    {[0 ... 7] = 0},
    {[0] = 1},
    {[0] = 1, [4] = 1},
    {[0] = 1, [3] = 1, [6] = 1},
    {[0] = 1, [2] = 1, [4] = 1, [6] = 1},
    {[0 ... 2] = 1, [4 ... 6] = 1},
    {[0 ... 6] = 1}
};

static float drop_thresholds[] = {7.0 / 21,
				  6.0 / 21,
				  5.0 / 21,
				  4.0 / 21,
				  3.0 / 21,
				  2.0 / 21,
				  1.0 / 21,
				  0};

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
	while((!vo->frames && vo->state != STOP) || vo->state == PAUSE)
	    pthread_cond_wait(&vo->scd, &vo->smx);
	pthread_mutex_unlock(&vo->smx);

	if(vo->state == STOP)
	    break;

	if(vo->timer->wait(vo->timer, vo->pts[vo->tail]) < 0)
	    continue;

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

	pthread_mutex_lock(&vo->smx);
	if(vo->frames > 0){
	    if(++vo->tail == vo->driver->frames)
		vo->tail = 0;
	    vo->frames--;
	}
	pthread_cond_broadcast(&vo->scd);
	pthread_mutex_unlock(&vo->smx);
    }

    pthread_mutex_lock(&vo->smx);
    vo->state = STOP;  
    vo->head = vo->tail = 0;
    vo->frames = 0;
    pthread_cond_broadcast(&vo->scd);
    pthread_mutex_unlock(&vo->smx);

    return NULL;
}

static inline float
bufr(video_out_t *vo)
{
    return (float) vo->frames / vo->driver->frames;
}

static int
v_put(tcvp_pipe_t *p, packet_t *pk)
{
    video_out_t *vo = p->private;
    u_char *data[4];
    int strides[4];
    int planes;

    if(!vo->drop[vo->dropcnt]){
	pthread_mutex_lock(&vo->smx);
	while(vo->frames == vo->driver->frames && vo->state != STOP){
	    pthread_cond_wait(&vo->scd, &vo->smx);
	}
	pthread_mutex_unlock(&vo->smx);

	if(vo->state == STOP){
	    pk->free(pk);
	    return 0;
	}

	planes = vo->driver->get_frame(vo->driver, vo->head, data, strides);
	vo->cconv(vo->vstream->height, pk->data, pk->sizes, data, strides);
	if(vo->driver->put_frame)
	    vo->driver->put_frame(vo->driver, vo->head);
	vo->pts[vo->head] = pk->pts;

	pthread_mutex_lock(&vo->smx);
	vo->frames++;
	if(++vo->head == vo->driver->frames)
	    vo->head = 0;
	pthread_cond_broadcast(&vo->scd);
	pthread_mutex_unlock(&vo->smx);
    }

    if(output_video_conf_framedrop &&
       vo->framecnt > vo->driver->frames){
	int i;
	float bfr;

	if(++vo->dropcnt == DROPLEN)
	    vo->dropcnt = 0;

	bfr = bufr(vo);

	for(i = 0; bfr < drop_thresholds[i]; i++);
	vo->drop = drops[i];
    }

    vo->framecnt++;

    pk->free(pk);

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
	pthread_mutex_lock(&vo->smx);
	while(vo->frames)
	    pthread_cond_wait(&vo->scd, &vo->smx);
	pthread_mutex_unlock(&vo->smx);
    } else {
	pthread_mutex_lock(&vo->smx);
	vo->timer->interrupt(vo->timer);
	vo->tail = vo->head = 0;
	vo->frames = 0;
	if(vo->driver->flush)
	    vo->driver->flush(vo->driver);
	pthread_cond_broadcast(&vo->scd);
	pthread_mutex_unlock(&vo->smx);
    }

    vo->framecnt = 0;

    return 0;
}

static int
v_buffer(tcvp_pipe_t *p, float r)
{
    video_out_t *vo = p->private;

    pthread_mutex_lock(&vo->smx);
    while(bufr(vo) < r)
	pthread_cond_wait(&vo->scd, &vo->smx);
    pthread_mutex_unlock(&vo->smx);

    return 0;
}

static int
v_free(tcvp_pipe_t *p)
{
    video_out_t *vo = p->private;

    pthread_mutex_lock(&vo->smx);
    vo->state = STOP;
    pthread_cond_broadcast(&vo->scd);
    pthread_mutex_unlock(&vo->smx);

    vo->timer->interrupt(vo->timer);
    pthread_join(vo->thr, NULL);

    v_flush(p, 1);

    vo->driver->close(vo->driver);

    pthread_mutex_destroy(&vo->smx);
    pthread_cond_destroy(&vo->scd);

    free(vo->pts);
    free(vo);
    free(p);

    return 0;
}

static color_conv_t conv_table[PIXEL_FORMATS+1][PIXEL_FORMATS+1] = {
    [PIXEL_FORMAT_I420][PIXEL_FORMAT_YV12] = i420_yv12,
    [PIXEL_FORMAT_YV12][PIXEL_FORMAT_I420] = yv12_i420,
    [PIXEL_FORMAT_I420][PIXEL_FORMAT_YUY2] = i420_yuy2,
};

extern tcvp_pipe_t *
v_open(video_stream_t *vs, conf_section *cs, timer__t *timer)
{
    tcvp_pipe_t *pipe;
    video_out_t *vo;
    video_driver_t *vd = NULL;
    color_conv_t cconv = NULL;
    int i;

    for(i = 0; i < output_video_conf_driver_count; i++){
	driver_video_open_t vdo;
	char buf[256];

	sprintf(buf, "driver/video/%s", output_video_conf_driver[i].name);
	if(!(vdo = tc2_get_symbol(buf, "open")))
	    continue;

	if((vd = vdo(vs, cs))){
	    cconv = conv_table[vs->pixel_format][vd->pixel_format];
	    if(cconv){
		break;
	    } else {
		vd->close(vd);
		vd = NULL;
	    }
	}
    }

    if(!vd)
	return NULL;

    vo = calloc(1, sizeof(*vo));
    vo->driver = vd;
    vo->vstream = vs;
    vo->timer = timer;
    vo->cconv = cconv;
    vo->pts = malloc(vd->frames * sizeof(*vo->pts));
    vo->head = 0;
    vo->tail = 0;
    pthread_mutex_init(&vo->smx, NULL);
    pthread_cond_init(&vo->scd, NULL);
    vo->state = PAUSE;
    vo->drop = drops[0];
    pthread_create(&vo->thr, NULL, v_play, vo);

    pipe = malloc(sizeof(*pipe));
    pipe->input = v_put;
    pipe->start = v_start;
    pipe->stop = v_stop;
    pipe->free = v_free;
    pipe->flush = v_flush;
    pipe->buffer = v_buffer;
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
