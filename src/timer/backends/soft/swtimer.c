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
#include <stdint.h>
#include <sys/time.h>
#include <tctypes.h>
#include <pthread.h>
#include <tcalloc.h>
#include <swtimer_tc2.h>

typedef struct sw_timer {
    int res;
    int state;
    tcvp_timer_t *timer;
    pthread_t th;
    pthread_mutex_t mx;
} sw_timer_t;

#define RUN   1
#define PAUSE 2
#define STOP  3

static int
st_start(timer_driver_t *t)
{
    sw_timer_t *st = t->private;
    st->state = RUN;
    return 0;
}

static int
st_stop(timer_driver_t *t)
{
    sw_timer_t *st = t->private;
    st->state = PAUSE;
    return 0;
}

static void
st_free(void *p)
{
    timer_driver_t *t = p;
    sw_timer_t *st = t->private;
    st->state = STOP;
    pthread_join(st->th, NULL);
    pthread_mutex_destroy(&st->mx);
    free(st);
}

static int
st_settimer(timer_driver_t *t, tcvp_timer_t *tt)
{
    sw_timer_t *st = t->private;
    pthread_mutex_lock(&st->mx);
    st->timer = tt;
    pthread_mutex_unlock(&st->mx);
    return 0;
}

static void *
timer_run(void *p)
{
    sw_timer_t *st = p;
    struct timeval stime;
    struct timespec time;
    pthread_mutex_t mx;
    pthread_cond_t cd;

    pthread_mutex_init(&mx, NULL);
    pthread_cond_init(&cd, NULL);
    pthread_mutex_lock(&mx);

    gettimeofday(&stime, NULL);
    time.tv_sec = stime.tv_sec;
    time.tv_nsec = stime.tv_usec * 1000;

    while(st->state != STOP){
	time.tv_nsec += (st->res * 1000) / 27;
	if(time.tv_nsec > 1000000000){
	    time.tv_sec++;
	    time.tv_nsec -= 1000000000;
	}

	pthread_cond_timedwait(&cd, &mx, &time);

	pthread_mutex_lock(&st->mx);
	if(st->state == RUN && st->timer){
	    st->timer->tick(st->timer, st->res);
	}
	pthread_mutex_unlock(&st->mx);
    }

    pthread_mutex_unlock(&mx);
    pthread_mutex_destroy(&mx);
    pthread_cond_destroy(&cd);

    return NULL;
}

extern timer_driver_t *
st_new(int res)
{
    timer_driver_t *tm;
    sw_timer_t *st;

    st = calloc(1, sizeof(*st));
    st->res = res;
    pthread_mutex_init(&st->mx, NULL);
    st->state = PAUSE;

    pthread_create(&st->th, NULL, timer_run, st);

    tm = tcallocdz(sizeof(*tm), NULL, st_free);
    tm->start = st_start;
    tm->stop = st_stop;
    tm->set_timer = st_settimer;
    tm->private = st;

    return tm;
}
