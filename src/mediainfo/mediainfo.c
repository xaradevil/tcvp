/**
    Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

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
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <pthread.h>
#include <sys/time.h>
#include <mediainfo_tc2.h>


typedef struct tcvp_mi {
    eventq_t control;
    tcconf_section_t *conf;
    muxed_stream_t *current;
    tcvp_pl_content_event_t *playlist;
    tcvp_module_t *dbc;
} tcvp_mi_t;

static char *
escape_string(char *src)
{
    char *ret = malloc(2*strlen(src)+1);
    char *dst = ret;
    do {
	if (*src == '\'') *dst++ = '\\';
	*dst++ = *src;
    } while(*src++);

    return ret;
}

extern int
get_info(tcvp_module_t *m, char *t)
{
    tcvp_mi_t *h = m->private;

    muxed_stream_t *ms;
    ms = stream_open(t, h->conf, NULL);
    if(ms) {
	tcattr_t *a = tcallocz(sizeof(*t)*100);
	int n = tcattr_getall(ms, 99, a);
	int i;
	for(i=0; i<n; i++) {
	    char *c, *te, *ne, *ve;
	    te = escape_string(t);
	    ne = escape_string(a[i].name);
	    ve = escape_string(a[i].value);
	    c = malloc(strlen(ne) + strlen(ve) +
		       strlen(t) + 100);
	    sprintf(c, "ADD '%s/%s' '%s'", te, ne, ve);
	    tc2_print("mediainfo", TC2_PRINT_DEBUG+10, "%s\n", c);
	    tcdb_reply_t *r = tcvp_tcdbc_query(h->dbc, c);
	    tcfree(r);
	    free(c);
	    free(te);
	    free(ne);
	    free(ve);
	}
	tcfree(a);
	tcfree(ms);
    }

    return 0;
}


extern int
mi_pl_content(tcvp_module_t *m, tcvp_event_t *te)
{
    int i;

    tcvp_pl_content_event_t *playlist = (tcvp_pl_content_event_t *)te;

    for(i = 0; i < playlist->length; i++){
	tc2_print("mediainfo", TC2_PRINT_DEBUG+9, "%s\n", playlist->names[i]);
	get_info(m, playlist->names[i]);
    }

    return 0;
}


static void
mi_free(void *p)
{
    tcvp_mi_t *mi = p;

    tc2_print("mediainfo", TC2_PRINT_DEBUG+1, "mi_free\n");

    tcfree(mi->dbc);

    if(mi->control)
	eventq_delete(mi->control);
    tcfree(mi->conf);
}


extern int
mi_init(tcvp_module_t *m)
{
    tcvp_mi_t *mi = m->private;
    char *qname, *qn;

    if(!tcconf_getvalue(mi->conf, "features/mediainfo", ""))
	return -1;

    tc2_print("mediainfo", TC2_PRINT_DEBUG+1, "mi_init\n");

    qname = tcvp_event_get_qname(mi->conf);
    qn = alloca(strlen(qname) + 9);

    mi->control = eventq_new(NULL);

    sprintf(qn, "%s/control", qname);
    eventq_attach(mi->control, qn, EVENTQ_SEND);

    tcconf_setvalue(mi->conf, "features/mediainfo", "");
    tcconf_setvalue(mi->conf, "features/local/mediainfo", "");

    mi->dbc = tcvp_tcdbc_new(mi->conf);
    mi->dbc->init(mi->dbc);

    free(qname);
    return 0;
}


extern int
mi_new(tcvp_module_t *m, tcconf_section_t *cs)
{
    tcvp_mi_t *mi;

    tc2_print("mediainfo", TC2_PRINT_DEBUG+1, "mi_new\n");

    mi = tcallocdz(sizeof(*mi), NULL, mi_free);
    mi->conf = tcref(cs);
    m->private = mi;

    return 0;
}
