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

#define ALSA_PCM_NEW_HW_PARAMS_API 1
#include <alsa/asoundlib.h>
#include <alsa/pcm_plugin.h>

#include <tcvp_types.h>
#include <alsa_tc2.h>

typedef struct alsa_out {
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hwp;
    int bpf;
} alsa_out_t;

typedef struct alsa_timer {
    snd_timer_t *timer;
    uint64_t time;
    pthread_mutex_t mx;
    pthread_cond_t cd;
    int state;
    pthread_t th;
} alsa_timer_t;

#define RUN  1
#define STOP 2

static int
alsa_start(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;

    if(snd_pcm_state(ao->pcm) == SND_PCM_STATE_PAUSED)
	snd_pcm_pause(ao->pcm, 0);

    return 0;
}

static int
alsa_stop(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;

    if(snd_pcm_state(ao->pcm) == SND_PCM_STATE_RUNNING)
	snd_pcm_pause(ao->pcm, 1);

    return 0;
}

static int
alsa_free(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;

    snd_pcm_drop(ao->pcm);
    snd_pcm_close(ao->pcm);
    snd_pcm_hw_params_free(ao->hwp);
    free(ao);
    free(p);

    return 0;
}

static int
alsa_play(tcvp_pipe_t *p, packet_t *pk)
{
    alsa_out_t *ao = p->private;
    size_t count = pk->sizes[0] / ao->bpf;
    u_char *data = pk->data[0];

    while(count > 0){
	int r = snd_pcm_writei(ao->pcm, data, count);
	if (r == -EAGAIN || (r >= 0 && r < count)) {
	    snd_pcm_wait(ao->pcm, 1000);
	} else if(r == -EPIPE){
	    fprintf(stderr, "ALSA: xrun\n");
	    snd_pcm_prepare(ao->pcm);
	} else if(r < 0){
	    fprintf(stderr, "ALSA: write error: %s\n", snd_strerror(r));
	    return -1;
	}
	if(r > 0){
	    count -= r;
	    data += r * ao->bpf;
	}
    }

    pk->free(pk);

    return 0;
}

static void
free_timer(timer__t *t)
{
    alsa_timer_t *at = t->private;
    at->state = STOP;
    pthread_join(at->th, NULL);
    snd_timer_close(at->timer);
    at->time = -1;
    pthread_cond_broadcast(&at->cd);
    sched_yield();
    pthread_mutex_destroy(&at->mx);
    pthread_cond_destroy(&at->cd);
    free(at);
}

static void *
run_timer(void *p)
{
    alsa_timer_t *at = p;
    snd_timer_t *timer = at->timer;
    struct pollfd *pfd;
    int pfds;
    snd_timer_read_t tr;

    pfds = snd_timer_poll_descriptors_count(timer);
    pfd = calloc(pfds, sizeof(struct pollfd));
    snd_timer_poll_descriptors(timer, pfd, pfds);

    snd_timer_start(timer);

    while(at->state == RUN){
	int s = poll(pfd, pfds, 1000);

	if(s == 0)
	    continue;
	else if(s < 0)
	    break;

	while(snd_timer_read(timer, &tr, sizeof(tr)) == sizeof(tr)){
	    pthread_mutex_lock(&at->mx);
	    at->time += tr.resolution * tr.ticks;
	    pthread_cond_broadcast(&at->cd);
	    pthread_mutex_unlock(&at->mx);
	}
    }

    snd_timer_stop(timer);
    free(pfd);

    return NULL;
}

static int
tm_noop(timer__t *t)
{
    return 0;
}

static int
tm_wait(timer__t *t, uint64_t time)
{
    alsa_timer_t *at = t->private;
    pthread_mutex_lock(&at->mx);
    while(at->time < time * 1000 && at->state == RUN)
	pthread_cond_wait(&at->cd, &at->mx);
    pthread_mutex_unlock(&at->mx);

    return 0;
}

static uint64_t
tm_read(timer__t *t)
{
    alsa_timer_t *at = t->private;
    return at->time / 1000;
}

static int
tm_reset(timer__t *t, uint64_t time)
{
    alsa_timer_t *at = t->private;
    pthread_mutex_lock(&at->mx);
    at->time = time * 1000;
    pthread_cond_broadcast(&at->cd);
    pthread_mutex_unlock(&at->mx);

    return 0;
}

static timer__t *
open_timer(snd_pcm_t *pcm)
{
    snd_pcm_info_t *ifo;
    snd_timer_t *timer;
    snd_timer_params_t *pm;
    alsa_timer_t *at;
    timer__t *tm;
    u_int card, dev, sdev;
    char name[128];

    snd_timer_params_alloca(&pm);
    snd_pcm_info_alloca(&ifo);
    snd_pcm_info(pcm, ifo);

    card = snd_pcm_info_get_card(ifo);
    dev = snd_pcm_info_get_device(ifo);
    sdev = snd_pcm_info_get_subdevice(ifo);

    sprintf(name, "hw:CLASS=%i,SCLASS=%i,CARD=%i,DEV=%i,SUBDEV=%i",
	    SND_TIMER_CLASS_PCM, SND_TIMER_SCLASS_NONE, card, dev, sdev);
    snd_timer_open(&timer, name, SND_TIMER_OPEN_NONBLOCK);

    snd_timer_params_set_auto_start(pm, 1);
    snd_timer_params_set_ticks(pm, 1);
    snd_timer_params(timer, pm);

    at = malloc(sizeof(*at));
    at->timer = timer;
    at->time = 0;
    pthread_mutex_init(&at->mx, NULL);
    pthread_cond_init(&at->cd, NULL);
    at->state = RUN;

    tm = malloc(sizeof(*tm));
    tm->start = tm_noop;
    tm->stop = tm_noop;
    tm->wait = tm_wait;
    tm->read = tm_read;
    tm->reset = tm_reset;
    tm->free = free_timer;
    tm->private = at;

    pthread_create(&at->th, NULL, run_timer, at);

    return tm;
}

static snd_pcm_route_ttable_entry_t ttable_6_2[] = {
    SND_PCM_PLUGIN_ROUTE_FULL/4, SND_PCM_PLUGIN_ROUTE_FULL/4,
    SND_PCM_PLUGIN_ROUTE_FULL/4, 0,
    SND_PCM_PLUGIN_ROUTE_FULL/4, SND_PCM_PLUGIN_ROUTE_FULL/4,
    0, SND_PCM_PLUGIN_ROUTE_FULL/4,
    SND_PCM_PLUGIN_ROUTE_FULL/4, 0,
    0, SND_PCM_PLUGIN_ROUTE_FULL/4
};

static snd_pcm_route_ttable_entry_t *ttables[7][7] = {
    [2][6] = ttable_6_2
};

extern tcvp_pipe_t *
alsa_open(audio_stream_t *as, char *device, timer__t **timer)
{
    tcvp_pipe_t *tp;
    alsa_out_t *ao;
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hwp;
    u_int rate = as->sample_rate, channels = as->channels, ptime;
    int tmp;

    if(!device)
	device = "hw:0,0";

    if(snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0) != 0)
	return NULL;

    snd_pcm_hw_params_malloc(&hwp);
    snd_pcm_hw_params_any(pcm, hwp);

    snd_pcm_hw_params_set_access(pcm, hwp,
				 SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(pcm, hwp, &rate, &tmp);
    snd_pcm_hw_params_set_channels_near(pcm, hwp, &channels);

    ptime = 10000;
    snd_pcm_hw_params_set_period_time_near(pcm, hwp, &ptime, &tmp);

    snd_pcm_hw_params(pcm, hwp);

    if(timer)
	*timer = open_timer(pcm);

    if(channels != as->channels){
	snd_pcm_t *rpcm;
	snd_pcm_hw_params_t *rp;

	if(!ttables[channels][as->channels]){
	    snd_pcm_close(pcm);
	    return NULL;
	}

	snd_pcm_route_open(&rpcm, "default", SND_PCM_FORMAT_S16_LE,
			   channels, ttables[channels][as->channels],
			   channels, as->channels, channels, pcm, 1);
	snd_pcm_hw_params_alloca(&rp);
	snd_pcm_hw_params_any(rpcm, rp);

	snd_pcm_hw_params_set_access(rpcm, rp,
				     SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(rpcm, rp, SND_PCM_FORMAT_S16_LE);
	snd_pcm_hw_params_set_rate_near(rpcm, rp, &rate, &tmp);
	snd_pcm_hw_params_set_channels(rpcm, rp, as->channels);

	snd_pcm_hw_params(rpcm, rp);

	pcm = rpcm;
    }

    snd_pcm_prepare(pcm);

    ao = malloc(sizeof(*ao));
    ao->pcm = pcm;
    ao->hwp = hwp;
    ao->bpf = as->channels * 2;

    tp = malloc(sizeof(*tp));
    tp->input = alsa_play;
    tp->start = alsa_start;
    tp->stop = alsa_stop;
    tp->free = alsa_free;
    tp->private = ao;

    return tp;
}
