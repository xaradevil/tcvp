/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <tcalloc.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <tcvp_types.h>
#include <watchdog_tc2.h>

typedef struct watchdog {
    pthread_t dog;
    int bone[2];
    int timeout;
} watchdog_t;

static void *
wd_watch(void *p)
{
    watchdog_t *w = p;
    char buf[4];

    for(;;){
	struct timeval t = { w->timeout / 1000, (w->timeout % 1000) * 1000 };
	fd_set fs;
	FD_ZERO(&fs);
	FD_SET(w->bone[0], &fs);
	if(!select(FD_SETSIZE, &fs, NULL, NULL, &t)){
	    fprintf(stderr, "WATCHDOG: timeout\n");
	    exit(1);
	}
	read(w->bone[0], buf, 1);
    }

    return NULL;
}

static int
wd_input(tcvp_pipe_t *p, packet_t *pk)
{
    watchdog_t *w = p->private;

    write(w->bone[1], "f", 1);
    p->next->input(p->next, pk);

    return 0;
}

static int
wd_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    p->format = *s;
    return p->next->probe(p->next, pk, &p->format);
}

static int
wd_flush(tcvp_pipe_t *p, int drop)
{
    return p->next->flush(p->next, drop);
}

static void
wd_free(void *p)
{
    tcvp_pipe_t *tp = p;
    watchdog_t *w = tp->private;

    pthread_cancel(w->dog);
    pthread_join(w->dog, NULL);
    close(w->bone[0]);
    close(w->bone[1]);
    free(w);
}

extern tcvp_pipe_t *
wd_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
       muxed_stream_t *ms)
{
    tcvp_pipe_t *p;
    watchdog_t *w;

    w = calloc(1, sizeof(*w));
    pipe(w->bone);
    w->timeout = watchdog_conf_timeout;
    tcconf_getvalue(cs, "timeout", "%i", &w->timeout);
    fcntl(w->bone[0], F_SETFL, O_NONBLOCK);
    pthread_create(&w->dog, NULL, wd_watch, w);

    p = tcallocdz(sizeof(*p), NULL, wd_free);
    p->format = *s;
    p->input = wd_input;
    p->probe = wd_probe;
    p->flush = wd_flush;
    p->private = w;

    return p;
}
