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
#include <httpd.h>
#include <http_tc2.h>

extern void httpdDestroy(httpd *server);

#define HTML_HEAD "\
<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"\n\
  \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n\
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n\
  <head>\n\
    <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/>\n\
    <link rel=\"stylesheet\" href=\"/tcvp.css\" type=\"text/css\"/>\n\
    <title>%s</title>\n\
  </head>\n\
  <body>\n"

#define HTML_FOOT "</div>\n</body>\n</html>\n"

typedef struct tcvp_http {
    httpd *httpd;
    eventq_t control;
    tcconf_section_t *conf;
    pthread_t th;
    int run;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int update;
    int state;
    muxed_stream_t *current;
    tcvp_pl_content_event_t *playlist;
    int plpos, plflags;
    char **titles;
} tcvp_http_t;

static const struct tcvp_http_control {
    char *label;
    char *href;
    int activestate;
    char *activelabel;
} controls[] = {
    { "Play", "play", TCVP_STATE_PLAYING },
    { "Stop", "stop", TCVP_STATE_END },
    { "Pause", "pause", TCVP_STATE_STOPPED, "Resume" },
    { "Next", "next", -1 },
    { "Previous", "prev", -1 },
    { }
};

static char *
lookup_attr(char *n, void *p)
{
    return tcattr_get(p, n);
}

static void
http_head(tcvp_module_t *m, char *title)
{
    tcvp_http_t *h = m->private;
    httpd *hd = h->httpd;
    int i;

    httpdPrintf(hd, HTML_HEAD, title);
    httpdPrintf(hd, "<div class=\"heading\">%s</div>\n", title);
    httpdOutput(hd, "<div class=\"control\">\n");

    for(i = 0; controls[i].label; i++){
	char *class, *label;
	if(h->state == controls[i].activestate){
	    class = "active";
	    label = controls[i].activelabel?
		controls[i].activelabel: controls[i].label;
	} else {
	    class = "";
	    label = controls[i].label;
	}
	httpdPrintf(hd, "<a class=\"%s\" href=\"%s\">%s</a>\n",
		    class, controls[i].href, label);
    }

    httpdOutput(hd, "</div>\n");
    httpdOutput(hd, "<div class=\"main\">\n");
}

static void
http_status(httpd *hd)
{
    tcvp_module_t *m = (tcvp_module_t *) hd->host;
    tcvp_http_t *h = m->private;
    char *title = NULL;
    int i;

#define printattr(l, a) do {						\
    char *v = tcattr_get(h->current, a);				\
    if(v)								\
	httpdPrintf(hd, "<tr><td>" l "</td><td>%s</td></tr>\n", v);	\
} while(0)

    httpdAddHeader(hd, "Cache-Control: must-revalidate");
    httpdAddHeader(hd, "Pragma: no-cache");

    if(strcmp(httpdRequestPath(hd), "/")){
	httpdSetResponse(hd, "302 Found");
	httpdAddHeader(hd, "Location: /");
	httpdOutput(hd, "<html><body>302</body></html>");
	return;
    }

    pthread_mutex_lock(&h->lock);
    while(h->update > 0){
	struct timeval tm;
	struct timespec ts;
	gettimeofday(&tm, NULL);
	ts.tv_sec = tm.tv_sec;
	ts.tv_nsec = tm.tv_usec * 1000 + 200000000;
	if(ts.tv_nsec > 1000000000){
	    ts.tv_sec++;
	    ts.tv_nsec -= 1000000000;
	}
	if(pthread_cond_timedwait(&h->cond, &h->lock, &ts))
	    break;
    }

    if(h->current)
	title = tcstrexp(tcvp_ui_http_conf_title, "{", "}", ':',
			 lookup_attr, h->current, 0);
    else
	title = strdup("TCVP");
    http_head(m, title);
    free(title);

    httpdOutput(hd, "<div class=\"box status\">\n");

    if(h->current){
	httpdOutput(hd, "<table>\n");
	printattr("Title", "title");
	printattr("Artist", "artist");
	printattr("Album", "album");
	printattr("Track", "track");
	httpdOutput(hd, "</table>\n");
    } else {
	httpdOutput(hd, "No file\n");
    }

    httpdOutput(hd, "</div>\n");

    if(h->playlist){
	httpdOutput(hd, "<div class=\"box playlist\">\n");
/* 	httpdOutput(hd, "<h2>Playlist</h2>\n"); */
	httpdOutput(hd, "<table>\n");
	for(i = 0; i < h->playlist->length; i++){
	    char *t =
		h->titles && h->titles[i]? h->titles[i]: h->playlist->names[i];
	    httpdPrintf(hd,
			"<tr%s><td><a href=\"jump?p=%i\">%s</a></td></tr>\n",
			i == h->plpos? " class=\"plcurrent\"": "", i, t);
	}
	httpdOutput(hd, "</table></div>\n");
    }

    pthread_mutex_unlock(&h->lock);
    httpdOutput(hd, HTML_FOOT);
}

#define http_send(name, event, up)			\
static void						\
http_##name(httpd *hd)					\
{							\
    tcvp_module_t *m = (tcvp_module_t *) hd->host;	\
    tcvp_http_t *h = m->private;			\
    tcvp_event_send(h->control, event);			\
    h->update = up;					\
    http_status(hd);					\
}

http_send(play, TCVP_PL_START, 2)
http_send(next, TCVP_PL_NEXT, 4)
http_send(prev, TCVP_PL_PREV, 4)
http_send(pause, TCVP_PAUSE, 1)

static void
http_stop(httpd *hd)
{
    tcvp_module_t *m = (tcvp_module_t *) hd->host;
    tcvp_http_t *h = m->private;
    tcvp_event_send(h->control, TCVP_PL_STOP);
    tcvp_event_send(h->control, TCVP_CLOSE);
    h->update = 2;
    http_status(hd);
}

static void
http_jump(httpd *hd)
{
    tcvp_module_t *m = (tcvp_module_t *) hd->host;
    tcvp_http_t *h = m->private;
    httpVar *v = httpdGetVariableByName(hd, "p");

    if(v){
	int p = strtol(v->value, NULL, 0);
	tcvp_event_send(h->control, TCVP_PL_SEEK, p, TCVP_PL_SEEK_ABS);
	h->update = 4;
    }

    http_status(hd);
}

static void *
http_run(void *p)
{
    tcvp_module_t *m = p;
    tcvp_http_t *h = m->private;
    httpd *hd = h->httpd;

    while(h->run){
	struct timeval tmo = { 1, 0 };
	int gc = httpdGetConnection(hd, &tmo);

	if(gc < 0)
	    break;
	if(gc == 0)
	    continue;

	if(httpdReadRequest(hd) < 0)
	    continue;
	tc2_print("HTTP", TC2_PRINT_DEBUG, "%s\n", httpdRequestPath(hd));
	httpdProcessRequest(hd);
	httpdEndRequest(hd);
    }

    return NULL;
}

static void
http_update(tcvp_module_t *m)
{
    tcvp_http_t *h = m->private;
    pthread_mutex_lock(&h->lock);
    h->update--;
    pthread_cond_broadcast(&h->cond);
    pthread_mutex_unlock(&h->lock);
}

extern int
http_state(tcvp_module_t *m, tcvp_event_t *te)
{
    tcvp_state_event_t *se = (tcvp_state_event_t *) te;
    tcvp_http_t *h = m->private;

    h->state = se->state;
    http_update(m);
    return 0;
}

extern int
http_load(tcvp_module_t *m, tcvp_event_t *te)
{
    tcvp_load_event_t *le = (tcvp_load_event_t *) te;
    tcvp_http_t *h = m->private;

    pthread_mutex_lock(&h->lock);
    tcfree(h->current);
    h->current = tcref(le->stream);
    pthread_mutex_unlock(&h->lock);

    http_update(m);
    return 0;
}

#define playlistformat tcvp_ui_http_conf_playlistformat

extern int
http_pl_content(tcvp_module_t *m, tcvp_event_t *te)
{
    tcvp_http_t *h = m->private;
    int i;

    pthread_mutex_lock(&h->lock);
    if(h->titles){
	for(i = 0; i < h->playlist->length; i++)
	    free(h->titles[i]);
	free(h->titles);
    }
    tcfree(h->playlist);
    h->playlist = tcref(te);
    if(playlistformat){
	h->titles = calloc(h->playlist->length, sizeof(*h->titles));
	for(i = 0; i < h->playlist->length; i++){
	    muxed_stream_t *ms =
		stream_open(h->playlist->names[i], h->conf, NULL);
	    if(ms){
		h->titles[i] = tcstrexp(playlistformat, "{", "}", ':',
					lookup_attr, ms, 0);
	    }
	    tcfree(ms);
	}
    }
    pthread_mutex_unlock(&h->lock);

    http_update(m);
    return 0;
}

extern int
http_pl_state(tcvp_module_t *m, tcvp_event_t *te)
{
    tcvp_pl_state_event_t *pse = (tcvp_pl_state_event_t *) te;
    tcvp_http_t *h = m->private;
    h->plpos = pse->current;
    h->plflags = pse->flags;

    http_update(m);
    return 0;
}

static void
http_free(void *p)
{
    tcvp_http_t *h = p;

    h->run = 0;
    if(h->th)
	pthread_join(h->th, NULL);

    h->httpd->host = NULL;
    httpdDestroy(h->httpd);

    if(h->control)
	eventq_delete(h->control);
    tcfree(h->conf);
    pthread_mutex_destroy(&h->lock);
    pthread_cond_destroy(&h->cond);
}

extern int
http_init(tcvp_module_t *m)
{
    tcvp_http_t *h = m->private;
    char *qname, *qn;
    httpd *hd;

    if(!tcconf_getvalue(h->conf, "features/http", ""))
	return -1;

    hd = httpdCreate(tcvp_ui_http_conf_listen.iface,
		     tcvp_ui_http_conf_listen.port);
    if(!hd)
	return -1;

    httpdAddWildcardContent(hd, "/", NULL, HTTP_DIR);
    httpdAddCContent(hd, "/", "status", HTTP_TRUE, NULL, http_status);
#define http_reg(name)\
    httpdAddCContent(hd, "/", #name, HTTP_FALSE, NULL, http_##name);
    http_reg(play);
    http_reg(stop);
    http_reg(pause);
    http_reg(next);
    http_reg(prev);
    http_reg(jump);

    free(hd->host);
    hd->host = (char *) m;
    h->httpd = hd;

    qname = tcvp_event_get_qname(h->conf);
    qn = alloca(strlen(qname) + 9);

    h->control = eventq_new(NULL);

    sprintf(qn, "%s/control", qname);
    eventq_attach(h->control, qn, EVENTQ_SEND);

    tcconf_setvalue(h->conf, "features/http", "");
    tcconf_setvalue(h->conf, "features/local/http", "");
    tcconf_setvalue(h->conf, "features/ui", "");
    tcconf_setvalue(h->conf, "features/local/ui", "");

    h->run = 1;
    pthread_create(&h->th, NULL, http_run, m);

    free(qname);
    return 0;
}

extern int
http_new(tcvp_module_t *m, tcconf_section_t *cs)
{
    tcvp_http_t *h;

    h = tcallocdz(sizeof(*h), NULL, http_free);
    h->conf = tcref(cs);
    pthread_mutex_init(&h->lock, NULL);
    pthread_cond_init(&h->cond, NULL);
    m->private = h;

    return 0;
}
