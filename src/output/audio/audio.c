/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

    Licensed under the Open Software License version 2.0
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <audio_tc2.h>

#define ptsqsize tcvp_output_audio_conf_pts_qsize

#define min(a, b)  ((a) < (b)? (a): (b))

#define RUN   1
#define STOP  2
#define PAUSE 3

typedef struct audio_out {
    audio_driver_t *driver;
    tcvp_timer_t *timer;
    int bpf;
    int rate;
    int state;
    u_char *buf, *head, *tail;
    int bufsize;
    int bbytes;
    pthread_mutex_t mx;
    pthread_cond_t cd;
    pthread_t pth;
    int data_end;
    struct {
	uint64_t pts;
	u_char *bp;
    } *ptsq;
    int pqh, pqt, pqc;
    tcconf_section_t *conf;
} audio_out_t;

static int
audio_start(tcvp_pipe_t *p)
{
    audio_out_t *ao = p->private;

    pthread_mutex_lock(&ao->mx);
    ao->state = RUN;
    ao->driver->start(ao->driver);
    pthread_cond_broadcast(&ao->cd);
    pthread_mutex_unlock(&ao->mx);

    return 0;
}

static int
audio_stop(tcvp_pipe_t *p)
{
    audio_out_t *ao = p->private;

    pthread_mutex_lock(&ao->mx);
    ao->driver->stop(ao->driver);
    ao->state = PAUSE;
    pthread_mutex_unlock(&ao->mx);

    return 0;
}

static void
audio_free(void *p)
{
    tcvp_pipe_t *tp = p;
    audio_out_t *ao = tp->private;

    pthread_mutex_lock(&ao->mx);
    ao->state = STOP;
    pthread_cond_broadcast(&ao->cd);
    pthread_mutex_unlock(&ao->mx);
    if(ao->pth)
	pthread_join(ao->pth, NULL);
    if(ao->driver)
	tcfree(ao->driver);
    if(ao->buf)
	free(ao->buf);
    free(ao->ptsq);
    free(ao);
}

static int
audio_flush(tcvp_pipe_t *p, int drop)
{
    audio_out_t *ao = p->private;

    pthread_mutex_lock(&ao->mx);

    if(drop){
	ao->head = ao->tail = ao->buf;
	ao->bbytes = 0;
    } else {
	while(ao->bbytes)
	    pthread_cond_wait(&ao->cd, &ao->mx);
    }

    if(ao->driver)
	ao->driver->flush(ao->driver, drop);
    ao->timer->interrupt(ao->timer);

    pthread_cond_broadcast(&ao->cd);
    pthread_mutex_unlock(&ao->mx);

    return 0;
}

static int
audio_input(tcvp_pipe_t *p, packet_t *pk)
{
    audio_out_t *ao = p->private;
    size_t count;
    u_char *data;
    int pts;

    if(!pk->data){
	ao->data_end = 1;
	tcfree(pk);
	pthread_mutex_lock(&ao->mx);
	if(!ao->bbytes && ao->driver){
	    tcfree(ao->driver);
	    ao->driver = NULL;
	}
	pthread_mutex_unlock(&ao->mx);
	return 0;
    }

    count = pk->sizes[0];
    data = pk->data[0];
    pts = pk->flags & TCVP_PKT_FLAG_PTS;

    while(count > 0){
	int bs;

	pthread_mutex_lock(&ao->mx);
	while(ao->bbytes == ao->bufsize && ao->state != STOP)
	    pthread_cond_wait(&ao->cd, &ao->mx);

	if(pts && ao->pqc < ptsqsize){
/* 	    fprintf(stderr, "AUDIO: pts %llu, pqc %i, bp %p\n", */
/* 		    pk->pts, ao->pqc, ao->head); */
	    ao->ptsq[ao->pqh].pts = pk->pts;
	    ao->ptsq[ao->pqh].bp = ao->head;
	    if(++ao->pqh == ptsqsize)
		ao->pqh = 0;
	    ao->pqc++;
	    pts = 0;
	}

	bs = min(count, ao->bufsize - ao->bbytes);
	bs = min(bs, ao->bufsize - (ao->head - ao->buf));
	memcpy(ao->head, data, bs);
	data += bs;
	count -= bs;
	ao->bbytes += bs;
	ao->head += bs;
	if(ao->head - ao->buf == ao->bufsize)
	    ao->head = ao->buf;
	pthread_cond_broadcast(&ao->cd);
	pthread_mutex_unlock(&ao->mx);
    }

    tcfree(pk);

    return 0;
}

static void *
audio_play(void *p)
{
    audio_out_t *ao = p;

    while(ao->state != STOP){
	int count, r;

	pthread_mutex_lock(&ao->mx);
	while((!ao->bbytes || ao->state == PAUSE) && ao->state != STOP)
	    pthread_cond_wait(&ao->cd, &ao->mx);

	if(ao->state == STOP){
	    pthread_mutex_unlock(&ao->mx);
	    break;
	}

	count = min(ao->bbytes, ao->bufsize - (ao->tail - ao->buf)) / ao->bpf;
	r = ao->driver->write(ao->driver, ao->tail, count);

	if(r > 0){
	    u_char *pt = ao->tail;
	    count -= r;
	    ao->bbytes -= r * ao->bpf;
	    ao->tail += r * ao->bpf;

	    if(ao->pqc){
		u_char *pp = ao->ptsq[ao->pqt].bp;
/* 		fprintf(stderr, "AUDIO: %p %p %p\n", pt, pp, ao->tail); */
		if(pt <= pp && pp <= ao->tail){
		    int bc = ao->tail - pp;
		    uint64_t d, t, tm;
		    int64_t dt;
		    int df = ao->driver->delay(ao->driver);
		    d = (uint64_t) (df - bc / ao->bpf) * 27000000 / ao->rate;
		    t = ao->ptsq[ao->pqt].pts - d;
		    tm = ao->timer->read(ao->timer);
		    dt = tm > t? tm - t: t - tm;
		    if(dt > tcvp_output_audio_conf_pts_threshold){
			ao->timer->reset(ao->timer, t);
/* 			fprintf(stderr, "AUDIO: pts = %llu, t = %llu, dt = %5lli\n", */
/* 				ao->ptsq[ao->pqt].pts, t, t - tm); */
		    }
		    while(ao->ptsq[ao->pqt].bp < ao->tail && ao->pqc){
			if(++ao->pqt == ptsqsize)
			    ao->pqt = 0;
			--ao->pqc;
		    }
		}
	    }

	    if(ao->tail - ao->buf == ao->bufsize)
		ao->tail = ao->buf;

	    pthread_cond_broadcast(&ao->cd);
	    pthread_mutex_unlock(&ao->mx);
	} else {
	    pthread_mutex_unlock(&ao->mx);
	    if(r == -EAGAIN || r == 0)
		ao->driver->wait(ao->driver, 1000);
	    else
		break;
	}

	if(!ao->bbytes && ao->data_end){
	    pthread_mutex_lock(&ao->mx);
	    if(ao->driver)
		tcfree(ao->driver);
	    ao->driver = NULL;
	    pthread_mutex_unlock(&ao->mx);
	}
    }

    return NULL;
}

static int
audio_buffer(tcvp_pipe_t *p, float r)
{
    audio_out_t *ao = p->private;

    pthread_mutex_lock(&ao->mx);
    while((float) ao->bbytes / ao->bufsize < r)
	pthread_cond_wait(&ao->cd, &ao->mx);
    pthread_mutex_unlock(&ao->mx);

    return 0;
}

static int
audio_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    audio_out_t *ao = p->private;
    audio_driver_t *ad = NULL;
    audio_stream_t *as = &s->audio;
    int ssize = 0;
    char *sf;
    int i;

    if(s->stream_type != STREAM_TYPE_AUDIO)
	return PROBE_FAIL;

    if(!(sf = strstr(as->codec, "pcm-")))
	return PROBE_FAIL;

    sf += 4;

    for(i = 0; i < output_audio_conf_driver_count; i++){
	driver_audio_new_t adn;
	char buf[256];

	snprintf(buf, 256, "driver/audio/%s",
		 output_audio_conf_driver[i].name);
	if(!(adn = tc2_get_symbol(buf, "new")))
	    continue;

	if((ad = adn(as, ao->conf, ao->timer))){
	    if(!strcmp(ad->format, sf)){
		break;
	    } else {
		tcfree(ad);
		ad = NULL;
	    }
	}
    }

    if(!ad)
	return PROBE_FAIL;

    if(!strncmp(ad->format, "s16", 3)){
	ssize = 2;
    } else if(!strncmp(ad->format, "u16", 3)){
	ssize = 2;
    } else if(!strncmp(ad->format, "u8", 2)){
	ssize = 1;
    } else {
	fprintf(stderr, "AUDIO: unsupported format %s\n", ad->format);
	goto err;
    }

    ao->driver = ad;
    ao->bpf = as->channels * ssize;
    ao->rate = as->sample_rate;
    ao->bufsize = ao->bpf * output_audio_conf_buffer_size;
    ao->buf = malloc(ao->bufsize);
    ao->head = ao->tail = ao->buf;
    pthread_create(&ao->pth, NULL, audio_play, ao);

    p->format = *s;

    return PROBE_OK;
err:
    tcfree(ad);
    return PROBE_FAIL;
}

extern tcvp_pipe_t *
audio_open(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *timer,
	   muxed_stream_t *ms)
{
    tcvp_pipe_t *tp;
    audio_out_t *ao;

    ao = calloc(1, sizeof(*ao));
    pthread_mutex_init(&ao->mx, NULL);
    pthread_cond_init(&ao->cd, NULL);
    ao->state = PAUSE;
    ao->timer = timer;
    ao->ptsq = calloc(ptsqsize, sizeof(*ao->ptsq));
    ao->conf = tcref(cs);

    tp = tcallocdz(sizeof(*tp), NULL, audio_free);
    tp->input = audio_input;
    tp->start = audio_start;
    tp->stop = audio_stop;
    tp->flush = audio_flush;
    tp->buffer = audio_buffer;
    tp->probe = audio_probe;
    tp->private = ao;

    return tp;
}

static int
drv_cmp(const void *p1, const void *p2)
{
    const output_audio_conf_driver_t *d1 = p1, *d2 = p2;
    return d2->priority - d1->priority;
}

extern int
a_init(char *arg)
{
    if(output_audio_conf_driver_count > 0){
	qsort(output_audio_conf_driver, output_audio_conf_driver_count,
	      sizeof(output_audio_conf_driver_t), drv_cmp);
    }

    return 0;
}