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
#include <alsamod.h>

typedef struct alsa_timer {
    snd_timer_t *timer;
    uint64_t time;
    int intr;
    int wait;
    pthread_mutex_t mx;
    pthread_cond_t cd;
    int state;
    pthread_t th;
} alsa_timer_t;

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
	int s = poll(pfd, pfds, 100);
	uint64_t t = 0;

	if(s == 0)
	    continue;
	else if(s < 0)
	    break;

	while(snd_timer_read(timer, &tr, sizeof(tr)) == sizeof(tr)){
	    t += tr.resolution * tr.ticks;
	}

	pthread_mutex_lock(&at->mx);
	at->time += t;
	pthread_cond_broadcast(&at->cd);
	pthread_mutex_unlock(&at->mx);
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
    int intr, wait;

    pthread_mutex_lock(&at->mx);
    wait = ++at->wait;
    while(at->time < time * 1000 && at->state == RUN &&
	  (intr = (wait > at->intr)))
	pthread_cond_wait(&at->cd, &at->mx);
    pthread_mutex_unlock(&at->mx);

    return !intr? -1: at->state == RUN? 0: -1;
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

static int
tm_intr(timer__t *t)
{
    alsa_timer_t *at = t->private;

    pthread_mutex_lock(&at->mx);
    at->intr = at->wait;
    pthread_cond_broadcast(&at->cd);
    pthread_mutex_unlock(&at->mx);

    return 0;
}

extern timer__t *
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
    at->intr = 0;
    at->wait = 0;
    pthread_mutex_init(&at->mx, NULL);
    pthread_cond_init(&at->cd, NULL);
    at->state = RUN;

    tm = malloc(sizeof(*tm));
    tm->start = tm_noop;
    tm->stop = tm_noop;
    tm->wait = tm_wait;
    tm->read = tm_read;
    tm->reset = tm_reset;
    tm->interrupt = tm_intr;
    tm->free = free_timer;
    tm->private = at;

    pthread_create(&at->th, NULL, run_timer, at);

    return tm;
}
