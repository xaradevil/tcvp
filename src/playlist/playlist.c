/**
    Copyright (C) 2003-2006  Michael Ahlberg, Måns Rullgård

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

static int pl_addauto_unlocked(tcvp_playlist_t *, char **files, int n, int p);

static int
pl_send_state(tcvp_playlist_t *tpl)
{
    int cur = -1;
    if(tpl->cur >= 0 && tpl->cur < tpl->nf)
	cur = tpl->order[tpl->cur];
    tcvp_event_send(tpl->ss, TCVP_PL_STATE, cur, tpl->state, tpl->flags);
    return 0;
}

static int
pl_add(tcvp_playlist_t *tpl, char **files, int n, int p)
{
    int nf;
    int i;

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
	int bl;

	if(buf[0] == '#')
	    continue;

	bl = strlen(buf);
	if(bl == 0)
	    continue;

	bl--;
	while(bl > 0 && (buf[bl] == '\n' || buf[bl] == '\r'))
	    buf[bl--] = 0;

	if(buf[0] == 0)
	    continue;

	if(buf[0] == '/' || strchr(buf, ':')){
	    strncpy(line, buf, 1024);
	    line[1023] = 0;
	} else {
	    snprintf(line, 1024, "%s/%s", d, buf);
	}

	n += pl_addauto_unlocked(tpl, lp, 1, pos + n);
    }

    free(l);
    plf->close(plf);

    return n;
}

static int
pl_addauto_unlocked(tcvp_playlist_t *tpl, char **files, int n, int p)
{
    int i, nadd = 0;

    if(p < 0)
	p = tpl->nf + p + 1;
    if(p < 0)
	p = 0;

    for(i = 0; i < n; i++){
	char *m = stream_magic_url(files[i]);
	if(!m)
	    continue;
	if(!strcmp(m, "application/x-playlist")){
	    int np = pl_addlist(tpl, files[i], p);
	    if(np > 0){
		p += np;
                nadd += np;
            }
	} else {
	    pl_add(tpl, files + i, 1, p++);
            nadd++;
	}
        free(m);
    }

    return nadd;
}

static int
pl_addauto(tcvp_playlist_t *tpl, char **files, int n, int p)
{
    int ret;
    pthread_mutex_lock(&tpl->lock);
    ret = pl_addauto_unlocked(tpl, files, n, p);
    pthread_mutex_unlock(&tpl->lock);
    return ret;
}

static int
pl_remove(tcvp_playlist_t *tpl, int s, int n)
{
    int i, j, nr;
    u_int un = n;

    tc2_print("PLAYLIST", TC2_PRINT_DEBUG, "pl_remove s=%i n=%i\n", s, n);

    pthread_mutex_lock(&tpl->lock);

    if(s >= tpl->nf)
	goto end;

    if(s < 0)
	s = tpl->nf + s;
    if(s < 0){
	n += s;
	s = 0;
    }

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
    if(tpl->cur >= s){
	if(tpl->cur >= s + nr)
	    tpl->cur -= nr;
	else
	    tpl->cur = s;
    }

  end:
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

    if(tpl->cur < tpl->nf){
	if(tpl->state == PLAYING){
	    tpl->state = STOPPED;
	    pl_start(tpl);
	} else {
	    muxed_stream_t *ms = stream_open(tpl->files[tpl->order[tpl->cur]],
					     tpl->conf, NULL);
	    if(ms){
		tcvp_event_send(tpl->ss, TCVP_LOAD, ms);
		tcfree(ms);
	    }
	}
    }

    if(tpl->state == END){
	tcvp_event_send(tpl->sc, TCVP_CLOSE);
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
    pl_addauto(tpl, te->names, te->n, te->pos);
    tcvp_event_send(tpl->ss, TCVP_PL_CONTENT, tpl->nf, tpl->files);
    return 0;
}

extern int
epl_addlist(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    tcvp_pl_addlist_event_t *te = (tcvp_pl_addlist_event_t *) e;
    pl_addauto(tpl, &te->name, 1, te->pos);
    tcvp_event_send(tpl->ss, TCVP_PL_CONTENT, tpl->nf, tpl->files);
    return 0;
}

extern int
epl_remove(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    tcvp_pl_remove_event_t *te = (tcvp_pl_remove_event_t *) e;
    pl_remove(tpl, te->start, te->n);
    tcvp_event_send(tpl->ss, TCVP_PL_CONTENT, tpl->nf, tpl->files);
    pl_send_state(tpl);
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

extern int
epl_seek(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_playlist_t *tpl = p->private;
    tcvp_pl_seek_event_t *se = (tcvp_pl_seek_event_t *) e;
    int pos = -1, i;

    if(se->how == TCVP_PL_SEEK_ABS)
	pos = se->offset;
    else if(se->how == TCVP_PL_SEEK_REL)
	pos = tpl->cur + se->offset;

    if(pos < 0 || pos >= tpl->nf)
	return 0;

    if(se->how == TCVP_PL_SEEK_ABS && tpl->flags & TCVP_PL_FLAG_SHUFFLE){
	for(i = 0; i < tpl->nf; i++){
	    if(tpl->order[i] == pos){
		pos = i;
		break;
	    }
	}
    }

    tpl->cur = pos;
    pl_next(tpl, 0);

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

    tpl->ss = tcvp_event_get_sendq(tpl->conf, "status");
    tpl->sc = tcvp_event_get_sendq(tpl->conf, "control");

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
    tpl->state = STOPPED;
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

static u_char *
serialize_list(int n, char **s, int *size)
{
    int sz = *size + 4;
    u_char *sb, *p;
    int i;

    for(i = 0; i < n; i++)
	sz += strlen(s[i]) + 1;

    sb = malloc(sz);
    p = sb + *size;

    st_unaligned32(htob_32(n), p);
    p += 4;
    for(i = 0; i < n; i++)
	p += sprintf(p, "%s", s[i]) + 1;

    *size = sz;
    return sb;
}

extern char **
deserialize_list(u_char *p, int size, int *count)
{
    int n, i;
    char **s;

    if(size < 4)
	return NULL;

    n = htob_32(unaligned32(p));
    p += 4;
    size -= 4;

    s = calloc(n, sizeof(*s));
    i = 0;
    while(i < n && size > 0){
	u_char *m = memchr(p, 0, size);
	if(!m)
	    break;
	s[i++] = p;
	m++;
	size -= m - p;
	p = m;
    }

    *count = i;
    return s;
}

extern u_char *
pl_add_ser(char *name, void *event, int *size)
{
    tcvp_pl_add_event_t *te = event;
    int s = strlen(name) + 1 + 4;
    u_char *sb, *p;

    sb = serialize_list(te->n, te->names, &s);
    p = sb;

    p += sprintf(p, "%s", name) + 1;
    st_unaligned32(htob_32(te->pos), p);

    *size = s;
    return sb;
}

extern void *
pl_add_deser(int type, u_char *event, int size)
{
    u_char *nm = memchr(event, 0, size);
    char **names;
    int n, pos;
    void *evt;

    if(!nm)
	return NULL;
    nm++;
    size -= nm - event;
    if(size < 4)
	return NULL;
    pos = htob_32(unaligned32(nm));
    nm += 4;
    size -= 4;

    names = deserialize_list(nm, size, &n);
    if(!names)
	return NULL;

    evt = tcvp_event_new(type, names, n, pos);
    free(names);

    return evt;
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
    int len = va_arg(args, int);
    char **n = va_arg(args, char **);
    int i;

    te->length = len;
    te->names = malloc(te->length * sizeof(*te->names));
    for(i = 0; i < te->length; i++)
	te->names[i] = strdup(n[i]);

    return te;
}

extern u_char *
pl_ct_ser(char *name, void *event, int *size)
{
    tcvp_pl_content_event_t *te = event;
    int s = strlen(name) + 1;
    u_char *sb;

    sb = serialize_list(te->length, te->names, &s);
    sprintf(sb, "%s", name);

    *size = s;
    return sb;
}

extern void *
pl_ct_deser(int type, u_char *event, int size)
{
    u_char *nm = memchr(event, 0, size);
    char **names;
    void *evt;
    int n;

    if(!nm)
	return NULL;
    nm++;
    size -= nm - event;
    names = deserialize_list(nm, size, &n);
    if(!names)
	return NULL;

    evt = tcvp_event_new(type, n, names);
    free(names);

    return evt;
}
