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
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <timer_tc2.h>

#define RUN   1
#define STOP  2
#define PAUSE 3

typedef struct atimer {
    timer_driver_t *driver;
    uint64_t time;
    int intr;
    int wait;
    pthread_mutex_t mx;
    pthread_cond_t cd;
    int state;
} atimer_t;

static void
free_timer(void *p)
{
    tcvp_timer_t *t = p;
    atimer_t *at = t->private;
    at->state = STOP;
    if(at->driver){
	at->driver->set_timer(at->driver, NULL);
	tcfree(at->driver);
    }
    at->time = -1;
    pthread_cond_broadcast(&at->cd);
    sched_yield();
    pthread_mutex_destroy(&at->mx);
    pthread_cond_destroy(&at->cd);
    free(at);
}

static int
tm_wait(tcvp_timer_t *t, uint64_t time, pthread_mutex_t *lock)
{
    atimer_t *at = t->private;
    int intr = 1, wait, l = 1;

    pthread_mutex_lock(&at->mx);
    wait = ++at->wait;
    while(at->time < time && at->state != STOP && (intr = (wait > at->intr))){
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
    atimer_t *at = t->private;
    return at->time;
}

static int
tm_reset(tcvp_timer_t *t, uint64_t time)
{
    atimer_t *at = t->private;

    pthread_mutex_lock(&at->mx);
    at->time = time;
    pthread_cond_broadcast(&at->cd);
    pthread_mutex_unlock(&at->mx);

    return 0;
}

static int
tm_intr(tcvp_timer_t *t)
{
    atimer_t *at = t->private;

    pthread_mutex_lock(&at->mx);
    at->intr = at->wait;
    pthread_cond_broadcast(&at->cd);
    pthread_mutex_unlock(&at->mx);

    return 0;
}

static int
tm_start(tcvp_timer_t *t)
{
    atimer_t *at = t->private;
    at->state = RUN;
    if(at->driver)
	at->driver->start(at->driver);
    return 0;
}

static int
tm_stop(tcvp_timer_t *t)
{
    atimer_t *at = t->private;
    at->state = PAUSE;
    if(at->driver)
	at->driver->stop(at->driver);
    return 0;
}

static void
tm_tick(tcvp_timer_t *t, uint64_t ticks)
{
    atimer_t *at = t->private;

    pthread_mutex_lock(&at->mx);
    if(at->state == RUN)
	at->time += ticks;
    pthread_cond_broadcast(&at->cd);
    pthread_mutex_unlock(&at->mx);
}

static int
tm_setdriver(tcvp_timer_t *t, timer_driver_t *td)
{
    atimer_t *at = t->private;

    if(at->driver){
	at->driver->set_timer(at->driver, NULL);
	at->driver->stop(at->driver);
	tcfree(at->driver);
    }

    at->driver = td;

    if(td){
	td->set_timer(td, t);
	if(at->state == RUN)
	    td->start(td);
    }

    t->have_driver = !!td;

    return 0;
}

extern tcvp_timer_t *
tm_new(tcconf_section_t *cf)
{
    tcvp_timer_t *tm;
    atimer_t *at;

    at = calloc(1, sizeof(*at));
    pthread_mutex_init(&at->mx, NULL);
    pthread_cond_init(&at->cd, NULL);
    at->state = PAUSE;

    tm = tcallocdz(sizeof(*tm), NULL, free_timer);
    tm->start = tm_start;
    tm->stop = tm_stop;
    tm->wait = tm_wait;
    tm->read = tm_read;
    tm->reset = tm_reset;
    tm->interrupt = tm_intr;
    tm->set_driver = tm_setdriver;
    tm->tick = tm_tick;
    tm->private = at;

    return tm;
}
