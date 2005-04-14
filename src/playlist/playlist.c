/**
    Copyright (C) 2003-2005  Michael Ahlberg, Måns Rullgård

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
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <pthread.h>
#include <sys/time.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <playlist_tc2.h>

typedef struct tcvp_playlist {
    char **files;
    int *order;
    int nf, af;
    int state;
    int cur;
    eventq_t sc, ss;
    pthread_mutex_t lock;
    uint32_t flags;
    tcconf_section_t *conf;
} tcvp_playlist_t;

#define STOPPED TCVP_PL_STATE_STOPPED
#define PLAYING TCVP_PL_STATE_PLAYING
#define END     TCVP_PL_STATE_END

#define min(a,b) ((a)<(b)?(a):(b))

static int
pl_send_state(tcvp_playlist_t *tpl)
{
    tcvp_event_send(tpl->ss, TCVP_PL_STATE, tpl->order[tpl->cur],
		    tpl->state, tpl->flags);
    return 0;
}

static int
pl_add(tcvp_playlist_t *tpl, char **files, int n, int p)
{
    int nf;
    int i;

    pthread_mutex_lock(&tpl->lock);

    if(p < 0)
	p = tpl->nf + p + 1;
    if(p < 0)
	p = 0;

    nf = tpl->nf + n;

    if(nf > tpl->af){
	tpl->af = nf + 16;
	tpl->files = realloc(tpl->files, tpl->af * sizeof(*tpl->files));
	tpl->order = realloc(tpl->order, tpl->af * sizeof(*tpl->order));
    }

    if(p < tpl->nf)
	memmove(tpl->files + p + n, tpl->files + p, n * sizeof(*tpl->files));

    for(i = 0; i < n; i++){
	tc2_print("PLAYLIST", TC2_PRINT_DEBUG, "adding file %s\n", files[i]);
	tpl->files[p + i] = strdup(files[i]);
    }

    if(tpl->flags & TCVP_PL_FLAG_SHUFFLE){
	struct timeval tv;
	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);
	nf = tpl->nf;
	for(i = 0; i < n; i++){
	    int j = 0;
	    if(tpl->cur < nf)
		j = rand() % (nf - tpl->cur);
	    if(tpl->cur < tpl->nf && tpl->state != END)
		j++;
	    memmove(tpl->order + tpl->cur + j + 1,
		    tpl->order + tpl->cur + j,
		    (nf - tpl->cur - j) * sizeof(*tpl->order));
	    tpl->order[tpl->cur + j] = nf++;
	}
    } else {
	for(i = p; i < nf; i++)
	    tpl->order[i] = i;
    }

    tpl->nf = nf;

    pthread_mutex_unlock(&tpl->lock);
    return 0;
}

static int
pl_addlist(tcvp_playlist_t *tpl, char *file, int pos)
{
    url_t *plf = url_open(file, "r");
    char buf[1024], *line = alloca(1024), **lp = &line;
    char *d, *l;
    int n = 0;

    tc2_print("PLAYLIST", TC2_PRINT_DEBUG, "adding list %s\n", file);

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
	    int bl = strlen(buf);
	    buf[bl-1] = 0;
	    if(buf[bl-2] == '\r')
		buf[bl-2] = 0;
	    if(buf[0] == '/' || strchr(buf, ':')){
		strncpy(line, buf, 1024);
		line[1023] = 0;
	    } else {
		snprintf(line, 1024, "%s/%s", d, buf);
	    }
	    pl_add(tpl, lp, 1, pos + n++);
	}
    }

    free(l);
    plf->close(plf);

    return n;
}

static int
pl_remove(tcvp_playlist_t *tpl, int s, int n)
{
    int i, j, nr;
    u_int un = n;

    tc2_print("PLAYLIST", TC2_PRINT_DEBUG, "pl_remove s=%i n=%i\n", s, n);

    pthread_mutex_lock(&tpl->lock);

    if(s < 0)
	s = tpl->nf + s + 1;
    if(s < 0)
	s = 0;

    nr = min(tpl->nf - s, un);

    tc2_print("PLAYLIST", TC2_PRINT_DEBUG, "removing %i of %i entries @%i\n",
	      nr, tpl->nf, s);

    for(i = 0; i < nr; i++)
	free(tpl->files[s + i]);

    memmove(tpl->files + s, tpl->files + s + nr,
	    (tpl->nf - s - nr) * sizeof(*tpl->files));
    for(i = 0, j = 0; i < tpl->nf; i++){
	if(tpl->order[i] < s)
	    tpl->order[j++] = tpl->order[i];
	else if(tpl->order[i] >= s + nr)
	    tpl->order[j++] = tpl->order[i] - nr;
    }
    tpl->nf -= nr;
    if(tpl->cur > tpl->nf)
	tpl->cur = tpl->nf;

    pthread_mutex_unlock(&tpl->lock);
    return 0;
}

static int
pl_shuffle(tcvp_playlist_t *tpl, int s)
{
    struct timeval tv;
    int i;

    if(s){
	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	for(i = 0; i < tpl->nf; i++){
	    int j = rand() % (i + 1);
	    int t = tpl->order[i];
	    tpl->order[i] = tpl->order[j];
	    tpl->order[j] = t;
	}
    } else {
	for(i = 0; i < tpl->nf; i++)
	    tpl->order[i] = i;
    }

    return 0;
}

static int
pl_flags(tcvp_playlist_t *tpl, uint32_t flags)
{
    uint32_t cf = tpl->flags ^ flags;

    if(cf & TCVP_PL_FLAG_SHUFFLE)
	pl_shuffle(tpl, flags & TCVP_PL_FLAG_SHUFFLE);

    tpl->flags = flags;
    pl_send_state(tpl);

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
    tcvp_event_send(tpl->sc, TCVP_OPEN, tpl->files[tpl->order[tpl->cur]]);
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
	if(tpl->flags & TCVP_PL_FLAG_LREPEAT){
	    c = c < 0? tpl->nf - 1: 0;
	} else {
	    c = c < 0? 0: tpl->nf;
	}
	if(tpl->state == PLAYING && !(tpl->flags & TCVP_PL_FLAG_LREPEAT)){
	    tpl->state = END;
	}
    }

    tpl->cur = c;

    pthread_mutex_unlock(&tpl->lock);

    if(tpl->state == PLAYING){
	tpl->state = STOPPED;
	pl_start(tpl);
    } else if(tpl->cur < tpl->nf){
	muxed_stream_t *ms = stream_open(tpl->files[tpl->order[tpl->cur]],
					 tpl->conf, NULL);
	if(ms){
	    tcvp_event_send(tpl->ss, TCVP_LOAD, ms);
	    tcfree(ms);
	}
    }

    pl_send_state(tpl);
    return ret;
}

extern int
epl_state(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    tcvp_state_event_t *te = (tcvp_state_event_t *) e;

    switch(te->state){
    case TCVP_STATE_ERROR:
	if(tcvp_playlist_conf_stop_on_error){
	    tpl->state = STOPPED;
	    tcvp_event_send(tpl->sc, TCVP_CLOSE);
	    pl_next(tpl, 0);
	} else {
	    tpl->state = PLAYING;
	    pl_next(tpl, 1);
	}
	break;

    case TCVP_STATE_END:
	if(tpl->state == PLAYING){
	    if(tpl->flags & TCVP_PL_FLAG_REPEAT)
		pl_start(tpl);
	    else
		pl_next(tpl, 1);
	}
	break;

    case TCVP_STATE_PLAYING:
	tpl->state = PLAYING;
	pl_send_state(tpl);
	break;
    }

    return 0;
}

extern int
epl_start(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;

    if(tpl->state != PLAYING)
	pl_start(tpl);

    return 0;
}

extern int
epl_stop(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    tpl->state = STOPPED;
    pl_send_state(tpl);
    return 0;
}

extern int
epl_next(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    pl_next(tpl, 1);
    return 0;
}

extern int
epl_prev(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    pl_next(tpl, -1);
    return 0;
}

extern int
epl_add(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    tcvp_pl_add_event_t *te = (tcvp_pl_add_event_t *) e;
    pl_add(tpl, te->names, te->n, te->pos);
    return 0;
}

extern int
epl_addlist(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    tcvp_pl_addlist_event_t *te = (tcvp_pl_addlist_event_t *) e;
    pl_addlist(tpl, te->name, te->pos);
    return 0;
}

extern int
epl_remove(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    tcvp_pl_remove_event_t *te = (tcvp_pl_remove_event_t *) e;
    pl_remove(tpl, te->start, te->n);
    return 0;
}

extern int
epl_flags(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    tcvp_pl_flags_event_t *te = (tcvp_pl_flags_event_t *) e;
    uint32_t flags = tpl->flags;

    switch(te->op){
    case TCVP_PL_FLAGS_SET:
	flags = te->flags;
	break;
    case TCVP_PL_FLAGS_OR:
	flags |= te->flags;
	break;
    case TCVP_PL_FLAGS_AND:
	flags &= te->flags;
	break;
    case TCVP_PL_FLAGS_XOR:
	flags ^= te->flags;
	break;
    default:
	tc2_print("PLAYLIST", TC2_PRINT_WARNING,
		  "unknown flag op %i\n", te->op);
	break;
    }

    pl_flags(tpl, flags);

    return 0;
}

extern int
epl_query(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;

    tcvp_event_send(tpl->ss, TCVP_PL_CONTENT, tpl->nf, tpl->files);
    pl_send_state(tpl);
    return 0;
}

static void
pl_free(void *p)
{
    tcvp_playlist_t *tpl = p;
    int i;

    eventq_delete(tpl->ss);
    eventq_delete(tpl->sc);

    for(i = 0; i < tpl->nf; i++)
	free(tpl->files[i]);
    free(tpl->files);
    free(tpl->order);

    pthread_mutex_destroy(&tpl->lock);

    tcfree(tpl->conf);
}

extern int
pl_init(tcvp_module_t *m)
{
    tcvp_playlist_t *tpl = m->private;
    char *qname, *qn;

    if(!tcconf_getvalue(tpl->conf, "features/playlist", ""))
	return -1;

    qname = tcvp_event_get_qname(tpl->conf);
    qn = alloca(strlen(qname) + 9);

    tpl->ss = eventq_new(NULL);
    tpl->sc = eventq_new(NULL);

    sprintf(qn, "%s/control", qname);
    eventq_attach(tpl->sc, qn, EVENTQ_SEND);

    sprintf(qn, "%s/status", qname);
    eventq_attach(tpl->ss, qn, EVENTQ_SEND);

    tcconf_setvalue(tpl->conf, "features/playlist", "");
    tcconf_setvalue(tpl->conf, "features/local/playlist", "");

    free(qname);
    return 0;
}

extern int
pl_new(tcvp_module_t *m, tcconf_section_t *cs)
{
    tcvp_playlist_t *tpl;

    tpl = tcallocdz(sizeof(*tpl), NULL, pl_free);
    tpl->af = 16;
    tpl->files = malloc(tpl->af * sizeof(*tpl->files));
    tpl->order = malloc(tpl->af * sizeof(*tpl->order));
    pthread_mutex_init(&tpl->lock, NULL);
    tpl->state = END;
    tpl->conf = tcref(cs);
    m->private = tpl;

    return 0;
}

extern void
pl_free_add(void *p)
{
    tcvp_pl_add_event_t *te = p;
    int i;

    for(i = 0; i < te->n; i++)
	free(te->names[i]);
    free(te->names);
}

extern void *
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

extern u_char *
pl_add_ser(char *name, void *event, int *size)
{
    tcvp_pl_add_event_t *te = event;
    int s = strlen(name) + 1 + 4 + 4;
    u_char *sb, *p;
    int i;

    for(i = 0; i < te->n; i++)
	s += strlen(te->names[i]) + 1;

    sb = malloc(s);
    p = sb;

    p += sprintf(p, "%s", name) + 1;
    st_unaligned32(htob_32(te->n), p);
    p += 4;
    st_unaligned32(htob_32(te->pos), p);
    p += 4;
    for(i = 0; i < te->n; i++)
	p += sprintf(p, "%s", te->names[i]) + 1;

    *size = s;
    return sb;
}

extern void *
pl_add_deser(int type, u_char *event, int size)
{
    u_char *nm = memchr(event, 0, size);
    char **names;
    int n, i, pos;

    if(!nm)
	return NULL;
    nm++;
    size -= nm - event;
    if(size < 8)
	return NULL;
    n = htob_32(unaligned32(nm));
    nm += 4;
    pos = htob_32(unaligned32(nm));
    nm += 4;
    size -= 8;

    names = calloc(n, sizeof(*names));
    i = 0;
    while(i < n && size > 0){
	u_char *m = memchr(nm, 0, size);
	if(!m)
	    break;
	names[i++] = nm;
	m++;
	size -= m - nm;
	nm = m;
    }

    return tcvp_event_new(type, names, i, pos);
}

extern void
pl_content_free(void *p)
{
    tcvp_pl_content_event_t *te = p;
    int i;

    for(i = 0; i < te->length; i++)
	free(te->names[i]);
    free(te->names);
}

extern void *
pl_content_alloc(int t, va_list args)
{
    tcvp_pl_content_event_t *te =
	tcvp_event_alloc(t, sizeof(*te), pl_content_free);

    char **n = va_arg(args, char **);
    int i;
    te->length = va_arg(args, int);
    te->names = malloc(te->length * sizeof(*te->names));
    for(i = 0; i < te->length; i++)
	te->names[i] = strdup(n[i]);

    return te;
}
