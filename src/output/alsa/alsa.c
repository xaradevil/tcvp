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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <tcalloc.h>

#define ALSA_PCM_NEW_HW_PARAMS_API 1
#include <alsa/asoundlib.h>
#include <alsa/pcm_plugin.h>

#include <tcvp_types.h>
#include <alsa_tc2.h>
#include <alsamod.h>

#define ptsqsize tcvp_output_alsa_conf_pts_qsize

#define min(a, b)  ((a) < (b)? (a): (b))

typedef struct alsa_out {
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hwp;
    int bpf;
    int rate;
    timer__t *timer;
    int state;
    u_char *buf, *head, *tail;
    int bufsize;
    int bbytes;
    pthread_mutex_t mx;
    pthread_cond_t cd;
    pthread_t pth;
    int can_pause;
    int data_end;
    struct {
	uint64_t pts;
	u_char *bp;
    } *ptsq;
    int pqh, pqt, pqc;
} alsa_out_t;

static int
alsa_start(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;
    int s;

    pthread_mutex_lock(&ao->mx);
    ao->state = RUN;

    s = snd_pcm_state(ao->pcm);
    if(s == SND_PCM_STATE_PAUSED)
	snd_pcm_pause(ao->pcm, 0);
    else if(s != SND_PCM_STATE_RUNNING && s != SND_PCM_STATE_PREPARED)
	snd_pcm_prepare(ao->pcm);

    pthread_cond_broadcast(&ao->cd);
    pthread_mutex_unlock(&ao->mx);

    return 0;
}

static int
alsa_stop(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;

    pthread_mutex_lock(&ao->mx);

    if(ao->can_pause && (snd_pcm_state(ao->pcm) == SND_PCM_STATE_RUNNING))
	snd_pcm_pause(ao->pcm, 1);
    ao->state = PAUSE;

    pthread_mutex_unlock(&ao->mx);

    return 0;
}

static void
alsa_free(void *p)
{
    tcvp_pipe_t *tp = p;
    alsa_out_t *ao = tp->private;

    pthread_mutex_lock(&ao->mx);
    ao->state = STOP;
    pthread_cond_broadcast(&ao->cd);
    pthread_mutex_unlock(&ao->mx);
    pthread_join(ao->pth, NULL);

    if(ao->hwp){
	snd_pcm_hw_params_free(ao->hwp);
	snd_pcm_drop(ao->pcm);
    }
    if(ao->pcm){
	snd_pcm_close(ao->pcm);
    }
    if(ao->timer)
	tm_stop(ao->timer);
    if(ao->buf)
	free(ao->buf);
    free(ao);
}

static int
alsa_flush(tcvp_pipe_t *p, int drop)
{
    alsa_out_t *ao = p->private;

    pthread_mutex_lock(&ao->mx);

    if(drop){
	ao->head = ao->tail = ao->buf;
	ao->bbytes = 0;
	snd_pcm_drop(ao->pcm);
    } else {
	while(ao->bbytes)
	    pthread_cond_wait(&ao->cd, &ao->mx);
	snd_pcm_drain(ao->pcm);
    }

    ao->timer->interrupt(ao->timer);

    pthread_cond_broadcast(&ao->cd);
    pthread_mutex_unlock(&ao->mx);

    return 0;
}

static int
alsa_input(tcvp_pipe_t *p, packet_t *pk)
{
    alsa_out_t *ao = p->private;
    size_t count;
    u_char *data;
    int pts;

    if(!pk){
	ao->data_end = 1;
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
/* 	    fprintf(stderr, "ALSA: pts %llu, pqc %i, bp %p\n", */
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

    pk->free(pk);

    return 0;
}

static void *
alsa_play(void *p)
{
    alsa_out_t *ao = p;

    while(ao->state != STOP){
	int count, r;

	pthread_mutex_lock(&ao->mx);
	while((!ao->bbytes || ao->state == PAUSE) && ao->state != STOP)
	    pthread_cond_wait(&ao->cd, &ao->mx);

	if(ao->state == STOP)
	    break;

	count = min(ao->bbytes, ao->bufsize - (ao->tail - ao->buf)) / ao->bpf;
	r = snd_pcm_writei(ao->pcm, ao->tail, count);

	if(r > 0){
	    u_char *pt = ao->tail;
	    count -= r;
	    ao->bbytes -= r * ao->bpf;
	    ao->tail += r * ao->bpf;

	    if(ao->pqc){
		u_char *pp = ao->ptsq[ao->pqt].bp;
/* 		fprintf(stderr, "ALSA: %p %p %p\n", pt, pp, ao->tail); */
		if(pt <= pp && pp <= ao->tail){
		    int bc = ao->tail - pp;
		    uint64_t d, t, tm;
		    int64_t dt;
		    snd_pcm_sframes_t df;
		    snd_pcm_delay(ao->pcm, &df);
		    d = (uint64_t) (df - bc / ao->bpf) * 1000000 / ao->rate;
		    t = ao->ptsq[ao->pqt].pts - d;
		    tm = ao->timer->read(ao->timer);
		    dt = tm > t? tm - t: t - tm;
		    if(dt > tcvp_output_alsa_conf_pts_threshold){
			ao->timer->reset(ao->timer, t);
/* 			fprintf(stderr, "ALSA: pts = %llu, t = %llu, dt = %5lli\n", */
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
	} else if(r == -EAGAIN){
	    pthread_mutex_unlock(&ao->mx);
	    snd_pcm_wait(ao->pcm, 1000);
	} else if(r < 0){
	    if((r = snd_pcm_prepare(ao->pcm)) < 0){
		fprintf(stderr, "ALSA: %s\n", snd_strerror(r));
		break;
	    }
	    pthread_mutex_unlock(&ao->mx);
	}
	if(!ao->bbytes && ao->data_end){
	    tm_settimer(ao->timer, SYSTEM);
	}
    }

    pthread_mutex_unlock(&ao->mx);

    return NULL;
}

static int
alsa_buffer(tcvp_pipe_t *p, float r)
{
    alsa_out_t *ao = p->private;

    pthread_mutex_lock(&ao->mx);
    while((float) ao->bbytes / ao->bufsize < r)
	pthread_cond_wait(&ao->cd, &ao->mx);
    pthread_mutex_unlock(&ao->mx);

    return 0;
}

static int
alsa_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    alsa_out_t *ao = p->private;
    audio_stream_t *as = (audio_stream_t *) s;
    snd_pcm_hw_params_t *hwp;
    snd_pcm_t *pcm = ao->pcm;
    u_int rate = as->sample_rate, channels = as->channels, ptime;
    int tmp;

    if(s->stream_type != STREAM_TYPE_AUDIO)
	return PROBE_FAIL;

    snd_pcm_hw_params_malloc(&hwp);
    snd_pcm_hw_params_any(pcm, hwp);

    snd_pcm_hw_params_set_access(pcm, hwp, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(pcm, hwp, &rate, &tmp);
    snd_pcm_hw_params_set_channels_near(pcm, hwp, &channels);

    ptime = 10000;
    snd_pcm_hw_params_set_period_time_near(pcm, hwp, &ptime, &tmp);

    if(snd_pcm_hw_params(pcm, hwp) < 0){
	fprintf(stderr, "ALSA: snd_pcm_hw_parameters failed\n");
	return PROBE_FAIL;
    }

    ao->hwp = hwp;
    ao->bpf = as->channels * 2;
    ao->rate = as->sample_rate;
    ao->bufsize = ao->bpf * output_audio_conf_buffer_size;
    ao->buf = malloc(ao->bufsize);
    ao->head = ao->tail = ao->buf;
    ao->can_pause = snd_pcm_hw_params_can_pause(hwp);

    p->format = *s;

    return PROBE_OK;
}

extern tcvp_pipe_t *
alsa_open(audio_stream_t *as, conf_section *cs, timer__t **timer)
{
    tcvp_pipe_t *tp;
    alsa_out_t *ao;
    snd_pcm_t *pcm;
    char *device = tcvp_output_alsa_conf_device;

    if(cs)
	conf_getvalue(cs, "audio/device", "%s", &device);

    if(snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)){
	fprintf(stderr, "ALSA: Can't open device '%s'\n", device);
	return NULL;
    }

    if(timer)
	*timer = open_timer(pcm);

    ao = calloc(1, sizeof(*ao));
    ao->pcm = pcm;
    pthread_mutex_init(&ao->mx, NULL);
    pthread_cond_init(&ao->cd, NULL);
    ao->state = PAUSE;
    if(timer)
	ao->timer = *timer;
    ao->ptsq = calloc(ptsqsize, sizeof(*ao->ptsq));
    pthread_create(&ao->pth, NULL, alsa_play, ao);

    tp = tcallocdz(sizeof(*tp), NULL, alsa_free);
    tp->input = alsa_input;
    tp->start = alsa_start;
    tp->stop = alsa_stop;
    tp->flush = alsa_flush;
    tp->buffer = alsa_buffer;
    tp->probe = alsa_probe;
    tp->private = ao;

    return tp;
}
