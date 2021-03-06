/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
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

#define RUN   1
#define STOP  2
#define PAUSE 3

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
        tc2_print("ALSA", TC2_PRINT_ERROR, "snd_timer_open(%s): %s\n",
                  name, snd_strerror(s));
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

    snd_config_update_free_global();
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
alsa_timer_new(tcconf_section_t *cf, int res)
{
    return open_timer(res, NULL);
}
