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
#include <timer_tc2.h>

typedef struct sw_timer {
    uint64_t time;
    int res;
    pthread_mutex_t mx;
    pthread_cond_t cd;
    int state;
    pthread_t th;
} sw_timer_t;

#define RUN   1
#define PAUSE 2
#define STOP  3

static int
tm_wait(timer__t *t, uint64_t time)
{
    sw_timer_t *st = t->private;
    pthread_mutex_lock(&st->mx);
    while(st->time < time)
	pthread_cond_wait(&st->cd, &st->mx);
    pthread_mutex_unlock(&st->mx);

    return st->time == -1? -1: 0;
}

static uint64_t
tm_read(timer__t *t)
{
    sw_timer_t *st = t->private;
    return st->time;
}

static int
tm_reset(timer__t *t, uint64_t time)
{
    sw_timer_t *st = t->private;
    pthread_mutex_lock(&st->mx);
    st->time = time;
    pthread_cond_broadcast(&st->cd);
    pthread_mutex_unlock(&st->mx);

    return 0;
}

static int
tm_start(timer__t *t)
{
    sw_timer_t *st = t->private;
    st->state = RUN;
    return 0;
}

static int
tm_stop(timer__t *t)
{
    sw_timer_t *st = t->private;
    st->state = PAUSE;
    return 0;
}

static int
tm_intr(timer__t *t)
{
    tm_reset(t, -1);
}

static void
tm_free(timer__t *t)
{
    sw_timer_t *st = t->private;
    st->state = STOP;
    pthread_join(st->th, NULL);
    pthread_mutex_destroy(&st->mx);
    pthread_cond_destroy(&st->cd);
    free(st);
    free(t);
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
	time.tv_nsec += st->res * 1000;
	if(time.tv_nsec > 1000000000){
	    time.tv_sec++;
	    time.tv_nsec -= 1000000000;
	}

	pthread_cond_timedwait(&cd, &mx, &time);

	if(st->state == RUN){
	    pthread_mutex_lock(&st->mx);
	    st->time += st->res;
	    pthread_cond_broadcast(&st->cd);
	    pthread_mutex_unlock(&st->mx);
	}
    }

    pthread_mutex_unlock(&mx);
    pthread_mutex_destroy(&mx);
    pthread_cond_destroy(&cd);

    return NULL;
}

extern timer__t *
timer_new(int res)
{
    timer__t *tm;
    sw_timer_t *st;

    st = calloc(1, sizeof(*st));
    st->time = 0;
    st->res = res;
    pthread_mutex_init(&st->mx, NULL);
    pthread_cond_init(&st->cd, NULL);
    st->state = PAUSE;

    pthread_create(&st->th, NULL, timer_run, st);

    tm = calloc(1, sizeof(*tm));
    tm->start = tm_start;
    tm->stop = tm_stop;
    tm->wait = tm_wait;
    tm->read = tm_read;
    tm->reset = tm_reset;
    tm->free = tm_free;
    tm->interrupt = tm_intr;
    tm->private = st;

    return tm;
}
