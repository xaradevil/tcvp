/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <audio_tc2.h>
#include <audiomod.h>

#define ptsqsize tcvp_output_audio_conf_pts_qsize

#define min(a, b)  ((a) < (b)? (a): (b))

#define RUN   1
#define STOP  2
#define PAUSE 3

typedef struct audio_out {
    audio_driver_t *driver;
    tcvp_timer_t *timer;
    int channels;
    int ibpf, obpf;
    int rate;
    int state;
    u_char *buf, *head, *tail;
    int bufsize;
    int bbytes;
    sndconv_t conv;
    pthread_mutex_t mx;
    pthread_cond_t cd;
    pthread_t pth;
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
    free(tp->format.common.codec);
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
	ao->pqh = ao->pqt = 0;
	ao->pqc = 0;
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
	tcfree(pk);
	pthread_mutex_lock(&ao->mx);
	while(ao->bbytes && ao->state == RUN)
	    pthread_cond_wait(&ao->cd, &ao->mx);
	ao->state = STOP;
	pthread_cond_broadcast(&ao->cd);
	pthread_mutex_unlock(&ao->mx);
	pthread_join(ao->pth, NULL);
	pthread_mutex_lock(&ao->mx);
	tcfree(ao->driver);
	ao->driver = NULL;
	pthread_mutex_unlock(&ao->mx);
	return 0;
    }

    count = pk->sizes[0];
    data = pk->data[0];
    pts = pk->flags & TCVP_PKT_FLAG_PTS;

    while(count >= ao->ibpf){
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

	bs = min(count / ao->ibpf, (ao->bufsize - ao->bbytes) / ao->obpf);
	bs = min(bs, (ao->bufsize - (ao->head - ao->buf)) / ao->obpf);
	ao->conv(ao->head, data, bs, ao->channels);
	data += bs * ao->ibpf;
	count -= bs * ao->ibpf;
	ao->bbytes += bs * ao->obpf;
	ao->head += bs * ao->obpf;
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

	count = min(ao->bbytes, ao->bufsize - (ao->tail - ao->buf)) / ao->obpf;
	r = ao->driver->write(ao->driver, ao->tail, count);

	if(r > 0){
	    u_char *pt = ao->tail;
	    count -= r;
	    ao->bbytes -= r * ao->obpf;
	    ao->tail += r * ao->obpf;

	    if(ao->pqc){
		u_char *pp = ao->ptsq[ao->pqt].bp;
/* 		fprintf(stderr, "AUDIO: %p %p %p\n", pt, pp, ao->tail); */
		if(pt <= pp && pp <= ao->tail){
		    int bc = ao->tail - pp;
		    uint64_t d, t, tm;
		    int64_t dt;
		    int df = ao->driver->delay(ao->driver);
		    d = (uint64_t) (df - bc / ao->obpf) * 27000000 / ao->rate;
		    t = ao->ptsq[ao->pqt].pts - d;
		    tm = ao->timer->read(ao->timer);
		    dt = tm > t? tm - t: t - tm;
		    if(dt > tcvp_output_audio_conf_pts_threshold){
			ao->timer->reset(ao->timer, t);
/* 			fprintf(stderr, "AUDIO: df = %i, pts = %llu, t = %llu, dt = %5lli\n", df, ao->ptsq[ao->pqt].pts / 27, t / 27, (int64_t)(t - tm) / 27); */
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
get_ssize(char *s)
{
    s++;

    if(!strncmp(s, "16", 2)){
	return 2;
    } else if(!strncmp(s, "32", 2)){
	return 4;
    } else if(!strncmp(s, "8", 1)){
	return 1;
    } else {
	return 0;
    }
}

static int
audio_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    audio_out_t *ao = p->private;
    audio_driver_t *ad = NULL;
    audio_stream_t *as = &p->format.audio;
    char **formats;
    sndconv_t conv = NULL;
    int issize, ossize;
    char *sf;
    int i, j;

    if(s->stream_type != STREAM_TYPE_AUDIO)
	return PROBE_FAIL;

    if(!(sf = strstr(s->common.codec, "pcm-")))
	return PROBE_FAIL;

    sf += 4;
    formats = audio_all_conv(sf);

    p->format = *s;
    as->codec = malloc(256);

    for(i = 0; i < output_audio_conf_driver_count && !ad; i++){
	driver_audio_new_t adn;
	char buf[256];

	snprintf(buf, 256, "driver/audio/%s",
		 output_audio_conf_driver[i].name);
	if(!(adn = tc2_get_symbol(buf, "new")))
	    continue;

	for(j = 0; formats[j] && !ad; j++){
	    snprintf(as->codec, 256, "audio/pcm-%s", formats[j]);
	    if((ad = adn(as, ao->conf, ao->timer))){
		if(!(conv = audio_conv(sf, ad->format))){
		    tcfree(ad);
		    ad = NULL;
		}
	    }
	}
    }

    if(!ad)
	return PROBE_FAIL;

    if(!(issize = get_ssize(sf)))
	goto err;

    if(!(ossize = get_ssize(ad->format)))
	goto err;

    ao->driver = ad;
    ao->ibpf = as->channels * issize;
    ao->obpf = as->channels * ossize;
    ao->channels = as->channels;
    ao->rate = as->sample_rate;
    ao->bufsize = ao->obpf * output_audio_conf_buffer_size;
    ao->buf = malloc(ao->bufsize);
    ao->head = ao->tail = ao->buf;
    ao->conv = conv;
    pthread_create(&ao->pth, NULL, audio_play, ao);

    return PROBE_OK;
err:
    fprintf(stderr, "AUDIO: unsupported format %s\n", ad->format);
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
