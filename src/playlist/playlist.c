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
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <pthread.h>
#include <sys/time.h>
#include <tcvp_types.h>
#include <tcvp_event.h>
#include <playlist_tc2.h>

typedef struct tcvp_playlist {
    char **files;
    int nf, af;
    int state;
    int cur;
    eventq_t qr, sc, ss;
    pthread_t eth;
    pthread_mutex_t lock;
} tcvp_playlist_t;

#define STOPPED 0
#define PLAYING 1

#define min(a,b) ((a)<(b)?(a):(b))

static int
pl_add(playlist_t *pl, char **files, int n, int p)
{
    tcvp_playlist_t *tpl = pl->private;
    int nf;
    int i;

    pthread_mutex_lock(&tpl->lock);

    nf = tpl->nf + n;

    if(nf > tpl->af){
	tpl->files = realloc(tpl->files, (nf + 16) * sizeof(*tpl->files));
	tpl->af = nf + 16;
    }

    if(p < tpl->nf)
	memmove(tpl->files + p + n, tpl->files + p, n * sizeof(*tpl->files));

    for(i = 0; i < n; i++)
	tpl->files[p + i] = strdup(files[i]);

    tpl->nf = nf;

    pthread_mutex_unlock(&tpl->lock);
    return 0;
}

static int
pl_addlist(playlist_t *pl, char *file, int pos)
{
    FILE *plf = fopen(file, "r");
    char buf[1024], *line = alloca(1024), **lp = &line;
    char *d, *l;
    int n = 0;

    if(!plf)
	return -1;

    l = strdup(file);
    d = strrchr(l, '/');

    if(d){
	*d = 0;
	d = l;
    } else {
	d = ".";
    }

    while(fgets(buf, 1024, plf)){
	if(buf[0] != '#'){
	    buf[strlen(buf)-1] = 0;
	    if(buf[0] == '/') {
		strncpy(line, buf, 1024);
		line[1023] = 0;
	    } else {
		snprintf(line, 1024, "%s/%s", d, buf);
	    }
	    pl_add(pl, lp, 1, pos + n++);
	}
    }

    return n;
}

static int
pl_remove(playlist_t *pl, int s, int n)
{
    tcvp_playlist_t *tpl = pl->private;
    int i, nr;

    pthread_mutex_lock(&tpl->lock);

    nr = min(tpl->nf - s, n);

    for(i = 0; i < nr; i++)
	free(tpl->files[s + i]);

    memmove(tpl->files + s, tpl->files + s + nr, nr * sizeof(*tpl->files));

    pthread_mutex_unlock(&tpl->lock);
    return 0;
}

static int
pl_get(playlist_t *pl, char **d, int s, int n)
{
    tcvp_playlist_t *tpl = pl->private;
    int i, ng;

    pthread_mutex_lock(&tpl->lock);

    ng = min(tpl->nf - s,  n);

    for(i = 0; i < ng; i++)
	d[i] = strdup(tpl->files[s + i]);

    return ng;
}

static int
pl_count(playlist_t *pl)
{
    tcvp_playlist_t *tpl = pl->private;

    return tpl->nf;
}

static int
pl_shuffle(playlist_t *pl, int s, int n)
{
    tcvp_playlist_t *tpl = pl->private;
    struct timeval tv;
    int i;

    n = min(tpl->nf - s, n);
    gettimeofday(&tv, NULL);
    srand(tv.tv_usec);

    for(i = 0; i < n; i++){
	int j = rand() % (i + 1);
	char *t = tpl->files[i];
	tpl->files[i] = tpl->files[j];
	tpl->files[j] = t;
    }

    return 0;
}

static int
pl_start(tcvp_playlist_t *tpl)
{
    tcvp_open_event_t *oe;
    tcvp_event_t *te;

    if(!tpl->nf)
	return -1;

    pthread_mutex_lock(&tpl->lock);

    if(tpl->cur >= tpl->nf)
	tpl->cur = 0;

    te = tcvp_alloc_event(TCVP_CLOSE);
    eventq_send(tpl->sc, te);
    tcfree(te);

    oe = tcvp_alloc_event(TCVP_OPEN);
    oe->file = tpl->files[tpl->cur];
    eventq_send(tpl->sc, oe);
    tcfree(oe);

    te = tcvp_alloc_event(TCVP_START);
    eventq_send(tpl->sc, te);
    tcfree(te);

    pthread_mutex_unlock(&tpl->lock);
    return 0;
}

static int
pl_next(tcvp_playlist_t *tpl, int dir)
{
    int c, ret = 0;

    pthread_mutex_lock(&tpl->lock);

    c = tpl->cur + dir;

    if(c >= tpl->nf || c < 0){
	if(tpl->state == PLAYING){
	    tcvp_state_event_t *te;
	    tpl->state = STOPPED;
	    te = tcvp_alloc_event(TCVP_STATE);
	    te->state = TCVP_STATE_PL_END;
	    eventq_send(tpl->ss, te);
	    tcfree(te);
	}
	goto out;
    }

    tpl->cur = c;

out:
    pthread_mutex_unlock(&tpl->lock);

    if(tpl->state == PLAYING){
	tpl->state = STOPPED;
	pl_start(tpl);
    }

    return ret;
}

static void *
pl_event(void *p)
{
    tcvp_playlist_t *tpl = p;
    int run = 1;

    while(run){
	tcvp_event_t *te = eventq_recv(tpl->qr);
	switch(te->type){
	case TCVP_STATE:
	    switch(te->state.state){
	    case TCVP_STATE_ERROR:
	    case TCVP_STATE_END:
		if(tpl->state == PLAYING)
		    pl_next(tpl, 1);
		break;

	    case TCVP_STATE_PLAYING:
		tpl->state = PLAYING;
		break;
	    }
	    break;

	case TCVP_PL_START:
	    pl_start(tpl);
	    break;

	case TCVP_PL_STOP:
	    tpl->state = STOPPED;
	    break;

	case TCVP_PL_NEXT:
	    pl_next(tpl, 1);
	    break;

	case TCVP_PL_PREV:
	    pl_next(tpl, -1);
	    break;

	case -1:
	    run = 0;
	    break;
	}
	tcfree(te);
    }

    return NULL;
}

static void
pl_free(playlist_t *pl)
{
    tcvp_playlist_t *tpl = pl->private;
    tcvp_event_t *te;
    int i;

    te = tcvp_alloc_event(-1);
    eventq_send(tpl->qr, te);
    tcfree(te);
    pthread_join(tpl->eth, NULL);

    eventq_delete(tpl->qr);
    eventq_delete(tpl->ss);
    eventq_delete(tpl->sc);

    for(i = 0; i < tpl->nf; i++)
	free(tpl->files[i]);
    free(tpl->files);

    pthread_mutex_destroy(&tpl->lock);

    free(tpl);
    free(pl);
}

extern playlist_t *
pl_new(conf_section *cs)
{
    tcvp_playlist_t *tpl;
    playlist_t *pl;
    char *qname, *qn;

    tpl = calloc(1, sizeof(*tpl));
    tpl->af = 16;
    tpl->files = malloc(tpl->af * sizeof(*tpl->files));
    pthread_mutex_init(&tpl->lock, NULL);

    conf_getvalue(cs, "qname", "%s", &qname);
    qn = alloca(strlen(qname) + 9);

    tpl->qr = eventq_new(tcref);
    tpl->ss = eventq_new(NULL);
    tpl->sc = eventq_new(NULL);

    sprintf(qn, "%s/control", qname);
    eventq_attach(tpl->qr, qn, EVENTQ_RECV);
    eventq_attach(tpl->sc, qn, EVENTQ_SEND);

    sprintf(qn, "%s/status", qname);
    eventq_attach(tpl->qr, qn, EVENTQ_RECV);
    eventq_attach(tpl->ss, qn, EVENTQ_SEND);

    pthread_create(&tpl->eth, NULL, pl_event, tpl);

    pl = calloc(1, sizeof(*pl));
    pl->add = pl_add;
    pl->addlist = pl_addlist;
    pl->remove = pl_remove;
    pl->get = pl_get;
    pl->count = pl_count;
    pl->shuffle = pl_shuffle;
    pl->free = pl_free;
    pl->private = tpl;

    return pl;
}
