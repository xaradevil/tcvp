/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <pthread.h>
#include <sys/time.h>
#include <tcvp_types.h>
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

typedef union tcvp_pl_event {
    int type;
    tcvp_state_event_t state;
    tcvp_pl_add_event_t pl_add;
    tcvp_pl_addlist_event_t pl_addlist;
    tcvp_pl_remove_event_t pl_remove;
    tcvp_pl_shuffle_event_t pl_shuffle;
} tcvp_pl_event_t;

#define STOPPED 0
#define PLAYING 1

#define min(a,b) ((a)<(b)?(a):(b))

static int TCVP_STATE;
static int TCVP_OPEN;
static int TCVP_START;
static int TCVP_CLOSE;
static int TCVP_PL_START;
static int TCVP_PL_STOP;
static int TCVP_PL_NEXT;
static int TCVP_PL_PREV;
static int TCVP_PL_ADD;
static int TCVP_PL_ADDLIST;
static int TCVP_PL_REMOVE;
static int TCVP_PL_SHUFFLE;

static int
pl_add(playlist_t *pl, char **files, int n, int p)
{
    tcvp_playlist_t *tpl = pl->private;
    int nf;
    int i;

    pthread_mutex_lock(&tpl->lock);

    if(p < 0)
	p = tpl->nf + p + 1;
    if(p < 0)
	p = 0;

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
    tcvp_playlist_t *tpl = pl->private;
    url_t *plf = url_open(file, "r");
    char buf[1024], *line = alloca(1024), **lp = &line;
    char *d, *l;
    int n = 0;

    if(!plf)
	return -1;

    if(pos < 0)
	pos = tpl->nf + pos + 1;
    if(pos < 0)
	pos = 0;

    l = strdup(file);
    d = strrchr(l, '/');
    if(d && !*(d + 1)){
	*d = 0;
	d = strrchr(l, '/');
    }
    if(d){
	*d = 0;
	d = l;
    } else {
	d = ".";
    }

    while(url_gets(buf, 1024, plf)){
	if(buf[0] != '#'){
	    buf[strlen(buf)-1] = 0;
	    if(buf[0] == '/' || strchr(buf, ':')){
		strncpy(line, buf, 1024);
		line[1023] = 0;
	    } else {
		snprintf(line, 1024, "%s/%s", d, buf);
	    }
	    pl_add(pl, lp, 1, pos + n++);
	}
    }

    free(l);
    plf->close(plf);

    return n;
}

static int
pl_remove(playlist_t *pl, int s, int n)
{
    tcvp_playlist_t *tpl = pl->private;
    int i, nr;

    pthread_mutex_lock(&tpl->lock);

    if(s < 0)
	s = tpl->nf + s + 1;
    if(s < 0)
	s = 0;

    nr = min(tpl->nf - s, (unsigned) n);

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

    ng = min(tpl->nf - s,  (unsigned) n);

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

    if(s < 0)
	s = tpl->nf + s + 1;
    if(s < 0)
	s = 0;

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
    if(!tpl->nf)
	return -1;

    pthread_mutex_lock(&tpl->lock);

    if(tpl->cur >= tpl->nf)
	tpl->cur = 0;

    tcvp_event_send(tpl->sc, TCVP_CLOSE);
    tcvp_event_send(tpl->sc, TCVP_OPEN, tpl->files[tpl->cur]);
    tcvp_event_send(tpl->sc, TCVP_START);

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
	    tpl->state = STOPPED;
	    tcvp_event_send(tpl->ss, TCVP_STATE, TCVP_STATE_PL_END);
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
    playlist_t *pl = p;
    tcvp_playlist_t *tpl = pl->private;
    int run = 1;

    while(run){
	tcvp_pl_event_t *te = eventq_recv(tpl->qr);
	if(te->type == TCVP_STATE){
	    switch(te->state.state){
	    case TCVP_STATE_ERROR:
		tpl->state = PLAYING;
		pl_next(tpl, 1);
		break;

	    case TCVP_STATE_END:
		if(tpl->state == PLAYING)
		    pl_next(tpl, 1);
		break;

	    case TCVP_STATE_PLAYING:
		tpl->state = PLAYING;
		break;
	    }
	} else if(te->type == TCVP_PL_START){
	    pl_start(tpl);
	} else if(te->type == TCVP_PL_STOP){
	    tpl->state = STOPPED;
	} else if(te->type == TCVP_PL_NEXT){
	    pl_next(tpl, 1);
	} else if(te->type == TCVP_PL_PREV){
	    pl_next(tpl, -1);
	} else if(te->type == TCVP_PL_ADD){
	    pl_add(pl, te->pl_add.names, te->pl_add.n, te->pl_add.pos);
	} else if(te->type == TCVP_PL_ADDLIST){
	    pl_addlist(pl, te->pl_addlist.name, te->pl_addlist.pos);
	} else if(te->type == TCVP_PL_REMOVE){
	    pl_remove(pl, te->pl_remove.start, te->pl_remove.n);
	} else if(te->type == TCVP_PL_SHUFFLE){
	    pl_shuffle(pl, te->pl_shuffle.start, te->pl_shuffle.n);
	} else if(te->type == -1){
	    run = 0;
	}
	tcfree(te);
    }

    return NULL;
}

static void
pl_free(playlist_t *pl)
{
    tcvp_playlist_t *tpl = pl->private;
    int i;

    tcvp_event_send(tpl->qr, -1);
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
pl_new(tcconf_section_t *cs)
{
    tcvp_playlist_t *tpl;
    playlist_t *pl;
    char *qname, *qn;

    tpl = calloc(1, sizeof(*tpl));
    tpl->af = 16;
    tpl->files = malloc(tpl->af * sizeof(*tpl->files));
    pthread_mutex_init(&tpl->lock, NULL);

    tcconf_getvalue(cs, "qname", "%s", &qname);
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

    free(qname);

    pl = calloc(1, sizeof(*pl));
    pl->add = pl_add;
    pl->addlist = pl_addlist;
    pl->remove = pl_remove;
    pl->get = pl_get;
    pl->count = pl_count;
    pl->shuffle = pl_shuffle;
    pl->free = pl_free;
    pl->private = tpl;

    pthread_create(&tpl->eth, NULL, pl_event, pl);

    return pl;
}

static void
pl_free_add(void *p)
{
    tcvp_pl_add_event_t *te = p;
    int i;

    for(i = 0; i < te->n; i++)
	free(te->names[i]);
    free(te->names);
}

static void *
pl_alloc_add(int t, va_list args)
{
    tcvp_pl_add_event_t *te = tcvp_event_alloc(t, sizeof(*te), pl_free_add);

    char **n = va_arg(args, char **);
    int i;
    te->n = va_arg(args, int);
    te->pos = va_arg(args, int);
    te->names = malloc(te->n * sizeof(*te->names));
    for(i = 0; i < te->n; i++)
	te->names[i] = strdup(n[i]);

    return te;
}

static void
pl_free_addlist(void *p)
{
    tcvp_pl_addlist_event_t *te = p;

    free(te->name);
}

static void *
pl_alloc_addlist(int t, va_list args)
{
    tcvp_pl_addlist_event_t *te =
	tcvp_event_alloc(t, sizeof(*te), pl_free_addlist);

    te->name = strdup(va_arg(args, char *));
    te->pos = va_arg(args, int);

    return te;
}

static void *
pl_alloc_remove(int t, va_list args)
{
    tcvp_pl_remove_event_t *te = tcvp_event_alloc(t, sizeof(*te), NULL);

    te->start = va_arg(args, int);
    te->n = va_arg(args, int);

    return te;
}

static void *
pl_alloc_shuffle(int t, va_list args)
{
    tcvp_pl_shuffle_event_t *te = tcvp_event_alloc(t, sizeof(*te), NULL);
    te->start = va_arg(args, int);
    te->n = va_arg(args, int);

    return te;
}

extern int
pl_init(char *p)
{
    TCVP_PL_START = tcvp_event_register("TCVP_PL_START", NULL);
    TCVP_PL_STOP = tcvp_event_register("TCVP_PL_STOP", NULL);
    TCVP_PL_NEXT = tcvp_event_register("TCVP_PL_NEXT", NULL);
    TCVP_PL_PREV = tcvp_event_register("TCVP_PL_PREV", NULL);
    TCVP_PL_ADD = tcvp_event_register("TCVP_PL_ADD", pl_alloc_add);
    TCVP_PL_ADDLIST = tcvp_event_register("TCVP_PL_ADDLIST", pl_alloc_addlist);
    TCVP_PL_REMOVE = tcvp_event_register("TCVP_PL_REMOVE", pl_alloc_remove);
    TCVP_PL_SHUFFLE = tcvp_event_register("TCVP_PL_SHUFFLE", pl_alloc_shuffle);

    TCVP_STATE = tcvp_event_get("TCVP_STATE");
    TCVP_OPEN = tcvp_event_get("TCVP_OPEN"); 
    TCVP_START = tcvp_event_get("TCVP_START");
    TCVP_CLOSE = tcvp_event_get("TCVP_CLOSE");

    return 0;
}
