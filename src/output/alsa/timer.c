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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <tcalloc.h>
#include <sched.h>

#define ALSA_PCM_NEW_HW_PARAMS_API 1
#include <alsa/asoundlib.h>
#include <alsa/pcm_plugin.h>

#include <tcvp_types.h>
#include <alsa_tc2.h>
#include <alsamod.h>

typedef struct alsa_timer {
    tcvp_timer_t *timer;
    snd_timer_t *hwtimer;
    struct pollfd *pfd;
    int npfd;
    int class;
    int state;
    pthread_t th;
} alsa_timer_t;

static void
atm_free(void *p)
{
    timer_driver_t *t = p;
    alsa_timer_t *at = t->private;

    at->state = STOP;
    pthread_join(at->th, NULL);
    snd_timer_close(at->hwtimer);
    free(at->pfd);
    free(at);
}

static void *
run_timer(void *p)
{
    timer_driver_t *tmr = p;
    alsa_timer_t *at = tmr->private;
    snd_timer_read_t tr;
    uint64_t rt = 0;

    while(at->state == RUN){
	uint64_t t = 0;
	int s;

	s = poll(at->pfd, at->npfd, 100);
	if(s == 0){
	    continue;
	} else if(s < 0){
	    break;
	}

	while(snd_timer_read(at->hwtimer, &tr, sizeof(tr)) == sizeof(tr)){
	    t += tr.resolution * tr.ticks;
	}

	if(at->timer){
	    t *= 27;
	    at->timer->tick(at->timer, t / 1000 + rt);
	    rt = t % 1000;
	}
    }

    return NULL;
}

static int
atm_start(timer_driver_t *t)
{
    alsa_timer_t *at = t->private;
    snd_timer_start(at->hwtimer);
    return 0;
}

static int
atm_stop(timer_driver_t *t)
{
    alsa_timer_t *at = t->private;
    sched_yield();
    if(at->class != SND_TIMER_CLASS_PCM)
	snd_timer_stop(at->hwtimer);
    return 0;
}

static int
atm_settimer(timer_driver_t *t, tcvp_timer_t *tt)
{
    alsa_timer_t *at = t->private;
    at->timer = tt;
    return 0;
}

static alsa_timer_t *
new_timer(int res, int class, int sclass, int card, int dev, int subdev)
{
    alsa_timer_t *atm;
    snd_timer_params_t *pm;
    snd_timer_info_t *inf;
    snd_timer_t *timer;
    char name[128];
    int r, s, t;

    res = 1000 * res / 27;

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
    r = snd_timer_info_get_resolution(inf);
    t = (r > 0 && r < res)? res / r: 1;
    snd_timer_params_set_ticks(pm, t);
    snd_timer_params_set_auto_start(pm, 1);
    snd_timer_params(timer, pm);

    atm = calloc(1, sizeof(*atm));
    atm->hwtimer = timer;
    atm->npfd = snd_timer_poll_descriptors_count(timer);
    atm->pfd = calloc(atm->npfd, sizeof(*atm->pfd));
    snd_timer_poll_descriptors(timer, atm->pfd, atm->npfd);
    atm->class = class;

    return atm;
}

extern timer_driver_t *
open_timer(int res, snd_pcm_t *pcm)
{
    snd_pcm_info_t *ifo;
    alsa_timer_t *at;
    timer_driver_t *tm;
    u_int card, dev, sdev;

    if(pcm){
	snd_pcm_info_alloca(&ifo);
	snd_pcm_info(pcm, ifo);

	card = snd_pcm_info_get_card(ifo);
	dev = snd_pcm_info_get_device(ifo);
	sdev = snd_pcm_info_get_subdevice(ifo);

	at = new_timer(res, SND_TIMER_CLASS_PCM, SND_TIMER_SCLASS_NONE,
		       card, dev, sdev);
    } else {
	at = new_timer(res, SND_TIMER_CLASS_GLOBAL,
		       SND_TIMER_SCLASS_NONE,
		       0, SND_TIMER_GLOBAL_SYSTEM, 0);
    }

    if(!at)
	return NULL;

    at->state = RUN;

    tm = tcallocdz(sizeof(*tm), NULL, atm_free);
    tm->start = atm_start;
    tm->stop = atm_stop;
    tm->set_timer = atm_settimer;
    tm->private = at;

    pthread_create(&at->th, NULL, run_timer, tm);

    return tm;
}

extern timer_driver_t *
alsa_timer_new(int res)
{
    return open_timer(res, NULL);
}
