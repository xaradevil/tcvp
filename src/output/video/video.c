/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <semaphore.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <video_tc2.h>
#include <vid_priv.h>

#define PLAY  1
#define PAUSE 2
#define STOP  3

typedef struct video_out {
    video_driver_t *driver;
    video_stream_t *vstream;
    tcvp_timer_t *timer;
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
    tcconf_section_t *conf;
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
/*     int64_t lpts = 0, dt, dpts, lt = 0, tm, dpt; */

    pthread_mutex_lock(&vo->smx);

    while(vo->state != STOP){
	while((!vo->frames && vo->state != STOP) || vo->state == PAUSE)
	    pthread_cond_wait(&vo->scd, &vo->smx);

	if(vo->state == STOP)
	    break;

	if(vo->pts[vo->tail] == -1LL)
	    break;

/* 	dpts = vo->pts[vo->tail] - lpts; */
/* 	lpts = vo->pts[vo->tail]; */
/* 	tm = vo->timer->read(vo->timer); */
/* 	dt = tm - lt; */
/* 	lt = tm; */
/* 	dpt = vo->pts[vo->tail] - tm; */

/* 	fprintf(stderr, "VO: pts = %llu, dt = %lli, dpts = %llu pts-t = %lli, buf = %i\n", vo->pts[vo->tail], dt / 27, dpts / 27, dpt, vo->frames); */

	if(vo->timer->wait(vo->timer, vo->pts[vo->tail], &vo->smx) < 0)
	    continue;

	vo->driver->show_frame(vo->driver, vo->tail);

	if(vo->frames > 0){
	    if(++vo->tail == vo->driver->frames)
		vo->tail = 0;
	    vo->frames--;
	    pthread_cond_broadcast(&vo->scd);
	}
    }

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

static void
v_qpts(video_out_t *vo, uint64_t pts)
{
    pthread_mutex_lock(&vo->smx);
    if(vo->framecnt){
	vo->pts[vo->head] = pts;
	vo->frames++;
	if(++vo->head == vo->driver->frames)
	    vo->head = 0;
	pthread_cond_broadcast(&vo->scd);
    }
    pthread_mutex_unlock(&vo->smx);
}

static int
v_put(tcvp_pipe_t *p, packet_t *pk)
{
    video_out_t *vo = p->private;
    u_char *data[4];
    int strides[4];
    int planes;

    if(!pk->data){
	v_qpts(vo, -1LL);
	goto out;
    }

    if(!vo->drop[vo->dropcnt]){
	pthread_mutex_lock(&vo->smx);
	vo->framecnt++;
	while(vo->frames == vo->driver->frames && vo->state != STOP)
	    pthread_cond_wait(&vo->scd, &vo->smx);
	pthread_mutex_unlock(&vo->smx);

	if(!vo->framecnt || vo->state == STOP){
	    goto out;
	}

	planes = vo->driver->get_frame(vo->driver, vo->head, data, strides);
	vo->cconv(vo->vstream->width, vo->vstream->height, pk->data,
		  pk->sizes, data, strides);
	if(vo->driver->put_frame)
	    vo->driver->put_frame(vo->driver, vo->head);
	v_qpts(vo, pk->pts);
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

out:
    tcfree(pk);
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

    pthread_mutex_lock(&vo->smx);
    vo->state = PAUSE;
    if(vo->timer)
	vo->timer->interrupt(vo->timer);
    pthread_mutex_unlock(&vo->smx);

    return 0;
}

static int
v_flush(tcvp_pipe_t *p, int drop)
{
    video_out_t *vo = p->private;

    pthread_mutex_lock(&vo->smx);

    if(!drop){
	while(vo->frames)
	    pthread_cond_wait(&vo->scd, &vo->smx);
	vo->framecnt = 0;
    } else {
	vo->tail = vo->head = 0;
	vo->frames = 0;
	if(vo->driver && vo->driver->flush)
	    vo->driver->flush(vo->driver);
	vo->framecnt = 0;
	if(vo->timer)
	    vo->timer->interrupt(vo->timer);
	pthread_cond_broadcast(&vo->scd);
    }

    pthread_mutex_unlock(&vo->smx);

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

static void
v_free(void *p)
{
    tcvp_pipe_t *tp = p;
    video_out_t *vo = tp->private;

    pthread_mutex_lock(&vo->smx);
    vo->state = STOP;
    pthread_cond_broadcast(&vo->scd);
    pthread_mutex_unlock(&vo->smx);

    if(vo->timer)
	vo->timer->interrupt(vo->timer);
    pthread_join(vo->thr, NULL);

    if(vo->driver)
	v_flush(p, 1);

    if(vo->driver)
	vo->driver->close(vo->driver);

    pthread_mutex_destroy(&vo->smx);
    pthread_cond_destroy(&vo->scd);

    if(vo->pts)
	free(vo->pts);
    tcfree(vo->conf);
    tcfree(vo->timer);
    free(vo);
}

static int
v_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    video_out_t *vo = p->private;
    video_driver_t *vd = NULL;
    color_conv_t cconv = NULL;
    video_stream_t *vs = (video_stream_t *) s;
    char *pf;
    int i;

    if(s->stream_type != STREAM_TYPE_VIDEO)
	return PROBE_FAIL;

    if(!(pf = strstr(vs->codec, "raw-")))
	return PROBE_FAIL;

    pf += 4;

    for(i = 0; i < output_video_conf_driver_count; i++){
	driver_video_open_t vdo;
	char buf[256];

	sprintf(buf, "driver/video/%s", output_video_conf_driver[i].name);
	if(!(vdo = tc2_get_symbol(buf, "open")))
	    continue;

	if((vd = vdo(vs, vo->conf))){
	    cconv = get_cconv(pf, vd->pixel_format);
	    if(cconv){
		break;
	    } else {
		vd->close(vd);
		vd = NULL;
	    }
	}
    }

    if(!vd)
	return PROBE_FAIL;

    p->format = *s;

    vo->driver = vd;
    vo->vstream = &p->format.video;
    vo->cconv = cconv;
    vo->pts = malloc(vd->frames * sizeof(*vo->pts));

    return PROBE_OK;
}

extern tcvp_pipe_t *
v_open(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *timer,
       muxed_stream_t *ms)
{
    tcvp_pipe_t *pipe;
    video_out_t *vo;

    vo = calloc(1, sizeof(*vo));
    vo->head = 0;
    vo->tail = 0;
    pthread_mutex_init(&vo->smx, NULL);
    pthread_cond_init(&vo->scd, NULL);
    vo->state = PAUSE;
    vo->drop = drops[0];
    vo->conf = tcref(cs);
    vo->timer = tcref(timer);
    pthread_create(&vo->thr, NULL, v_play, vo);

    pipe = tcallocdz(sizeof(*pipe), NULL, v_free);
    pipe->input = v_put;
    pipe->start = v_start;
    pipe->stop = v_stop;
    pipe->flush = v_flush;
    pipe->buffer = v_buffer;
    pipe->probe = v_probe;
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
