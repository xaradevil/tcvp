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

typedef struct alsa_hw_timer {
    snd_timer_t *timer;
    struct pollfd *pfd;
    int npfd;
    int class;
} alsa_hw_timer_t;

typedef struct alsa_timer {
    alsa_hw_timer_t *timers[2], *timer;
    uint64_t time;
    int intr;
    int wait;
    pthread_mutex_t mx;
    pthread_cond_t cd;
    int state;
    pthread_t th;
} alsa_timer_t;

static void
free_hwtimer(alsa_hw_timer_t *atm)
{
    snd_timer_close(atm->timer);
    free(atm->pfd);
    free(atm);
}

static void
free_timer(tcvp_timer_t *t)
{
    alsa_timer_t *at = t->private;
    at->state = STOP;
    pthread_join(at->th, NULL);
    if(at->timers[0])
	free_hwtimer(at->timers[0]);
    if(at->timers[1])
	free_hwtimer(at->timers[1]);
    at->time = -1;
    pthread_cond_broadcast(&at->cd);
    sched_yield();
    pthread_mutex_destroy(&at->mx);
    pthread_cond_destroy(&at->cd);
    free(at);
    free(t);
}

static void *
run_timer(void *p)
{
    tcvp_timer_t *tmr = p;
    alsa_timer_t *at = tmr->private;
    snd_timer_read_t tr;
    alsa_hw_timer_t *timer;

    while(at->state == RUN){
	int s;
	uint64_t t = 0;

	timer = at->timer;
	s = poll(timer->pfd, timer->npfd, 100);
	if(s == 0){
	    continue;
	} else if(s < 0){
	    break;
	}

	while(snd_timer_read(timer->timer, &tr, sizeof(tr)) == sizeof(tr)){
	    t += tr.resolution * tr.ticks;
	}

	pthread_mutex_lock(&at->mx);
	at->time += t;
	pthread_cond_broadcast(&at->cd);
	pthread_mutex_unlock(&at->mx);
    }

    return NULL;
}

static int
tm_wait(tcvp_timer_t *t, uint64_t time, pthread_mutex_t *lock)
{
    alsa_timer_t *at = t->private;
    int intr = 1, wait, l = 1;

    time = (time * 1000) / 27;

    pthread_mutex_lock(&at->mx);
    wait = ++at->wait;
    while(at->time < time && at->state == RUN && (intr = (wait > at->intr))){
	if(l && lock){
	    pthread_mutex_unlock(lock);
	    l = 0;
	}
	pthread_cond_wait(&at->cd, &at->mx);
    }
    pthread_mutex_unlock(&at->mx);
    if(!l && lock)
	pthread_mutex_lock(lock);

    return !intr? -1: at->state == RUN? 0: -1;
}

static uint64_t
tm_read(tcvp_timer_t *t)
{
    alsa_timer_t *at = t->private;
    return (at->time * 27) / 1000;
}

static int
tm_reset(tcvp_timer_t *t, uint64_t time)
{
    alsa_timer_t *at = t->private;

    pthread_mutex_lock(&at->mx);
    at->time = (time * 1000) / 27;
    pthread_cond_broadcast(&at->cd);
    pthread_mutex_unlock(&at->mx);

    return 0;
}

static int
tm_intr(tcvp_timer_t *t)
{
    alsa_timer_t *at = t->private;

    pthread_mutex_lock(&at->mx);
    at->intr = at->wait;
    pthread_cond_broadcast(&at->cd);
    pthread_mutex_unlock(&at->mx);

    return 0;
}

extern int
tm_stop(tcvp_timer_t *t)
{
    alsa_timer_t *at = t->private;
    pthread_mutex_lock(&at->mx);
    at->state = STOP;
    pthread_cond_broadcast(&at->cd);
    pthread_mutex_unlock(&at->mx);
    return 0;
}

static int
atm_start(tcvp_timer_t *t)
{
    alsa_timer_t *at = t->private;
    snd_timer_start(at->timer->timer);
    return 0;
}

static int
atm_stop(tcvp_timer_t *t)
{
    alsa_timer_t *at = t->private;
    if(at->timer->class != SND_TIMER_CLASS_PCM)
	snd_timer_stop(at->timer->timer);
    return 0;
}

extern int
tm_settimer(tcvp_timer_t *t, int type)
{
    alsa_timer_t *at = t->private;

    if(at->timers[type]){
	snd_timer_stop(at->timer->timer);
	at->timer = at->timers[type];
	snd_timer_start(at->timer->timer);
    }

    return 0;
}

static alsa_hw_timer_t *
new_timer(int class, int sclass, int card, int dev, int subdev)
{
    alsa_hw_timer_t *atm;
    snd_timer_params_t *pm;
    snd_timer_info_t *inf;
    snd_timer_t *timer;
    char name[128];
    int res, s;

    snd_timer_params_alloca(&pm);
    snd_timer_info_alloca(&inf);

    sprintf(name, "hw:CLASS=%i,SCLASS=%i,CARD=%i,DEV=%i,SUBDEV=%i",
	    class, sclass, card, dev, subdev);
    
    if((s = snd_timer_open(&timer, name, SND_TIMER_OPEN_NONBLOCK))){
	fprintf(stderr, "ALSA: snd_timer_open: %s\n",
		snd_strerror(s));
	return NULL;
    }

    snd_timer_info(timer, inf);
    res = snd_timer_info_get_resolution(inf);
    snd_timer_params_set_ticks(pm, res? 2000000 / res: 1);
    snd_timer_params_set_auto_start(pm, 1);
    snd_timer_params(timer, pm);

    atm = calloc(1, sizeof(*atm));
    atm->timer = timer;
    atm->npfd = snd_timer_poll_descriptors_count(timer);
    atm->pfd = calloc(atm->npfd, sizeof(*atm->pfd));
    snd_timer_poll_descriptors(timer, atm->pfd, atm->npfd);
    atm->class = class;

    return atm;
}

extern tcvp_timer_t *
open_timer(snd_pcm_t *pcm)
{
    snd_pcm_info_t *ifo;
    alsa_timer_t *at;
    tcvp_timer_t *tm;
    u_int card, dev, sdev;

    at = calloc(1, sizeof(*at));

    at->timers[SYSTEM] = new_timer(SND_TIMER_CLASS_GLOBAL,
				   SND_TIMER_SCLASS_NONE,
				   0, SND_TIMER_GLOBAL_SYSTEM, 0);
    if(pcm){
	snd_pcm_info_alloca(&ifo);
	snd_pcm_info(pcm, ifo);

	card = snd_pcm_info_get_card(ifo);
	dev = snd_pcm_info_get_device(ifo);
	sdev = snd_pcm_info_get_subdevice(ifo);

	at->timers[PCM] = new_timer(SND_TIMER_CLASS_PCM, SND_TIMER_SCLASS_NONE,
				    card, dev, sdev);
	at->timer = at->timers[PCM];
    }

    if(!pcm || !at->timers[PCM]){
	at->timer = at->timers[SYSTEM];
    }

    at->time = 0;
    at->intr = 0;
    at->wait = 0;
    pthread_mutex_init(&at->mx, NULL);
    pthread_cond_init(&at->cd, NULL);
    at->state = RUN;

    tm = calloc(1, sizeof(*tm));
    tm->start = atm_start;
    tm->stop = atm_stop;
    tm->wait = tm_wait;
    tm->read = tm_read;
    tm->reset = tm_reset;
    tm->interrupt = tm_intr;
    tm->free = free_timer;
    tm->private = at;

    pthread_create(&at->th, NULL, run_timer, tm);

    return tm;
}

extern tcvp_timer_t *
alsa_timer_new(int res)
{
    return open_timer(NULL);
}
