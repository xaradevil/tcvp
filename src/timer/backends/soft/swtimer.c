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
	time.tv_nsec += 1000 * st->res / 27;
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
st_new(tcconf_section_t *cf, int res)
{
    timer_driver_t *tm;
    sw_timer_t *st;

    st = calloc(1, sizeof(*st));
    st->res = 27000 * res;
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
