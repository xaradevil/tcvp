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
    uint64_t time;
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
    { "Previous", "prev", -1 },
    { "Next", "next", -1 },
    { "Shuffle", "shuffle", TCVP_PL_FLAG_SHUFFLE },
    { "Repeat", "repeat", TCVP_PL_FLAG_REPEAT },
    { "Loop", "loop", TCVP_PL_FLAG_LREPEAT },
    { }
};

#define playlistformat tcvp_ui_http_conf_playlistformat
#define playlistpage tcvp_ui_http_conf_playlistpage

#define min(a,b) ((a)<(b)?(a):(b))

static char *
lookup_attr(char *n, void *p)
{
    char *v = tcattr_get(p, n);
    return v? strdup(v): NULL;
}

static char *
exp_string(tcvp_http_t *h, muxed_stream_t *ms, char *s)
{
    return tcstrexp(s, "{", "}", ':', lookup_attr, ms,
		    TCSTREXP_FREE | TCSTREXP_ESCAPE);
}

static int
get_titles(tcvp_http_t *h, int start, int n)
{
    if(playlistformat && h->playlist){
	int i, end = min(start + n, h->playlist->length);
	for(i = start; i < end; i++){
	    muxed_stream_t *ms;
	    if(h->titles[i])
		continue;
	    ms = stream_open(h->playlist->names[i], h->conf, NULL);
	    if(ms)
		h->titles[i] = exp_string(h, ms, playlistformat);
	    tcfree(ms);
	}
    }

    return 0;
}

static int
http_redirect(tcvp_http_t *h, char *dest)
{
    httpd *hd = h->httpd;
    char buf[1024];

    if(!strcmp(httpdRequestPath(hd), dest))
	return 0;

    snprintf(buf, sizeof(buf), "Location: %s", dest);
    httpdSetResponse(hd, "302 Found");
    httpdAddHeader(hd, buf);
    httpdPrintf(hd, "<html><body><a href=\"%s\">%s</a></body></html>",
		buf, buf);
    return 1;
}

static void
http_head(tcvp_module_t *m, char *title)
{
    tcvp_http_t *h = m->private;
    httpd *hd = h->httpd;
    int refresh = tcvp_ui_http_conf_refresh;
    int i;

    httpdOutput(hd, "\
<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"\n\
  \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n\
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n\
  <head>\n\
    <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/>\n\
    <link rel=\"stylesheet\" href=\"tcvp.css\" type=\"text/css\"/>\n");

    if(h->current && h->current->time && h->state == TCVP_STATE_PLAYING){
	int d = (h->current->time - h->time) / 27000000 + 1;
	if(d < refresh)
	    refresh = d;
    }
    httpdPrintf(hd, "<meta http-equiv=\"refresh\" content=\"%i\"/>\n",
		refresh);

    httpdPrintf(hd, "<title>%s</title>\n", title);
    httpdOutput(hd, "</head>\n");
    httpdOutput(hd, "<body>\n");
    httpdPrintf(hd, "<div class=\"heading\">%s</div>\n", title);
    httpdOutput(hd, "<div class=\"control\">\n");

    for(i = 0; controls[i].label; i++){
	char *class, *label;
	if(i == 5)
	    httpdOutput(hd, "<hr/>");
	if((i <= 4 && h->state == controls[i].activestate) ||
	   (i > 4 && h->plflags & controls[i].activestate)){
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
http_print_info(tcvp_http_t *h)
{
    httpd *hd = h->httpd;
    int i;

    httpdOutput(hd, "<div class=\"box status\">\n");

    if(h->current){
	httpdOutput(hd, "<table>\n");
	for(i = 0; i < tcvp_ui_http_conf_info_count; i++){
	    char *l = tcvp_ui_http_conf_info[i].label;
	    char *v =
		exp_string(h, h->current, tcvp_ui_http_conf_info[i].value);
	    if(l[0] && v[0])
		httpdPrintf(hd, "<tr><td>%s</td><td>%s</td></tr>\n", l, v);
	    free(v);
	}
	httpdOutput(hd, "</table>\n");
    } else {
	httpdOutput(hd, "No file\n");
    }

    httpdOutput(hd, "</div>\n");
}

static void
http_print_plpages(tcvp_http_t *h)
{
    httpd *hd = h->httpd;
    int i;

    httpdOutput(hd, "<div><table class=\"plpage\"><tr>");
    for(i = 0; i < h->playlist->length; i += playlistpage){
	int e = min(i + playlistpage, h->playlist->length);
	char *class = h->plpos >= i && h->plpos < e? "plcurrent": "";
	httpdPrintf(hd, "<td class=\"%s\"><a href=\"page?ps=%i\">%i - %i</a>"
		    "</td>", class, i, i + 1, e);
    }
    httpdOutput(hd, "</tr></table></div>\n");
}

static void
http_print_playlist(tcvp_http_t *h, int start)
{
    httpd *hd = h->httpd;
    int end, i;

    httpdOutput(hd, "<div class=\"box playlist\">\n");
    if(h->playlist && h->playlist->length){
	end = min(start + playlistpage, h->playlist->length);
	get_titles(h, start, playlistpage);
	if(h->playlist->length > playlistpage)
	    http_print_plpages(h);
	httpdOutput(hd, "<div>\n");
	httpdOutput(hd, "<script language=\"JavaScript\" "
		      "type=\"text/javascript\">\n"
		    "<!--\n"
		    "function ToggleAll() {\n"
		    "  for (var i=0; i<document.plremove.elements.length; "
		      "i++) {\n"
		    "    if(document.plremove.elements[i].type == "
		      "'checkbox' && "
		      "document.plremove.elements[i].name != \"toggle\"){\n"
		    "      document.plremove.elements[i].checked = "
		      "!(document.plremove.elements[i].checked);\n"
		    "    }\n"
		    "  }\n"
		    "}\n"
		    "//-->\n"
		    "</script>\n");

	httpdOutput(hd, "<div><a href=\"javascript:void(0)\" "
		    "onClick=\"ToggleAll();\">Toggle All</a></div>\n");

	httpdOutput(hd, "<form action=\"remove\" method=\"get\" "
		    "name=\"plremove\">\n");
	httpdOutput(hd, "<table class=\"playlist\">\n");
	httpdOutput(hd, "<col id=\"plcheck\"/><col id=\"plnum\"/>"
		    "<col id=\"plname\"/>\n");
	for(i = start; i < end; i++){
	    char *t =
		h->titles && h->titles[i]? h->titles[i]: h->playlist->names[i];
	    char *class = i == h->plpos? "plcurrent" : "";
	    httpdPrintf(hd, "<tr class=\"%s\">", class);
	    httpdPrintf(hd, "<td>"
			"<input type=\"checkbox\" name=\"p\" value=\"%i\"/>"
			"</td>", i);
	    httpdPrintf(hd, "<td><a href=\"jump?p=%i\">%i</a></td>", i, i + 1);
	    httpdPrintf(hd, "<td><a href=\"jump?p=%i\">%s</a></td>", i, t);
	    httpdOutput(hd, "</tr>\n");
	}
	httpdOutput(hd, "</table>\n");
	httpdOutput(hd, "<div><input type=\"submit\" value=\"Remove\"/>"
		    "</div>\n");
	httpdOutput(hd, "</form>\n</div>\n");
	if(h->playlist->length > playlistpage)
	    http_print_plpages(h);
    } else {
	httpdOutput(hd, "<div>Empty playlist</div>\n");
    }
    httpdOutput(hd, "</div>\n");
}

static void
http_status(httpd *hd)
{
    tcvp_module_t *m = (tcvp_module_t *) hd->host;
    tcvp_http_t *h = m->private;
    char *title = NULL;
    httpVar *ps;
    int plstart = 0;

    if(http_redirect(h, "/"))
	return;

    ps = httpdGetVariableByName(hd, "s");
    if(ps)
	plstart = strtol(ps->value, NULL, 10);


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

    if(!h->playlist || plstart >= h->playlist->length)
	plstart = 0;

    if(h->current)
	title = exp_string(h, h->current, tcvp_ui_http_conf_title);
    else
	title = strdup("TCVP");
    http_head(m, title);
    free(title);

    http_print_info(h);
    http_print_playlist(h, plstart);

    pthread_mutex_unlock(&h->lock);
    httpdOutput(hd, "</div>\n</body>\n</html>\n");
}

#define http_send(name, up, ...)			\
static void						\
http_##name(httpd *hd)					\
{							\
    tcvp_module_t *m = (tcvp_module_t *) hd->host;	\
    tcvp_http_t *h = m->private;			\
    tcvp_event_send(h->control, __VA_ARGS__);		\
    h->update = up;					\
    http_redirect(h, "/");				\
}

http_send(play, 2, TCVP_PL_START)
http_send(next, 4, TCVP_PL_NEXT)
http_send(prev, 4, TCVP_PL_PREV)
http_send(pause, 1, TCVP_PAUSE)

#define http_plflag(name, flag)					\
    http_send(name, 1, TCVP_PL_FLAGS, flag, TCVP_PL_FLAGS_XOR)

http_plflag(shuffle, TCVP_PL_FLAG_SHUFFLE)
http_plflag(repeat, TCVP_PL_FLAG_REPEAT)
http_plflag(loop, TCVP_PL_FLAG_LREPEAT)

static void
http_stop(httpd *hd)
{
    tcvp_module_t *m = (tcvp_module_t *) hd->host;
    tcvp_http_t *h = m->private;
    tcvp_event_send(h->control, TCVP_PL_STOP);
    tcvp_event_send(h->control, TCVP_CLOSE);
    h->update = 2;
    http_redirect(h, "/");
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

    http_redirect(h, "/");
}

static void
http_page(httpd *hd)
{
    tcvp_module_t *m = (tcvp_module_t *) hd->host;
    tcvp_http_t *h = m->private;
    httpVar *ps;

    ps = httpdGetVariableByName(hd, "ps");
    if(ps)
	httpdSetCookie(hd, "s", ps->value);

    http_redirect(h, "/");
}

static void
http_remove(httpd *hd)
{
    tcvp_module_t *m = (tcvp_module_t *) hd->host;
    tcvp_http_t *h = m->private;
    httpVar *v = httpdGetVariableByName(hd, "p");
    int *r, i = 0;

    if(!h->playlist || !v)
	goto end;

    pthread_mutex_lock(&h->lock);

    r = calloc(2 * h->playlist->length, sizeof(*r));
    r[0] = strtol(v->value, NULL, 0);
    r[1] = 1;

    while((v = v->nextValue)){
	int p = strtol(v->value, NULL, 0);
	if(p > r[i] + r[i+1])
	    r[i+=2] = p;
	r[i+1]++;
    }

    while(i >= 0){
	tcvp_event_send(h->control, TCVP_PL_REMOVE, r[i], r[i+1]);
	h->update += 4;
	i -= 2;
    }

    free(r);

  end:
    pthread_mutex_unlock(&h->lock);
    http_redirect(h, "/");
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

	httpdAddHeader(hd, "Cache-Control: must-revalidate");
	httpdAddHeader(hd, "Pragma: no-cache");
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
    if(se->state == TCVP_STATE_END || se->state == TCVP_STATE_ERROR){
	pthread_mutex_lock(&h->lock);
	tcfree(h->current);
	if(h->playlist && h->plpos < h->playlist->length && h->plpos >= 0)
	    h->current = stream_open(h->playlist->names[h->plpos],
				     h->conf, NULL);
	else
	    h->current = NULL;
	pthread_mutex_unlock(&h->lock);
    }

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
    if(playlistformat)
	h->titles = calloc(h->playlist->length, sizeof(*h->titles));
    tcfree(h->current);
    if(h->playlist && h->plpos < h->playlist->length && h->plpos >= 0)
	h->current = stream_open(h->playlist->names[h->plpos],
				 h->conf, NULL);
    else
	h->current = NULL;
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

extern int
http_timer(tcvp_module_t *m, tcvp_event_t *te)
{
    tcvp_timer_event_t *t = (tcvp_timer_event_t *) te;
    tcvp_http_t *h = m->private;
    h->time = t->time;
    return 0;
}

static void
http_free(void *p)
{
    tcvp_http_t *h = p;
    int i;

    h->run = 0;
    if(h->th)
	pthread_join(h->th, NULL);

    if(h->httpd){
	h->httpd->host = NULL;
	httpdDestroy(h->httpd);
    }

    if(h->control)
	eventq_delete(h->control);
    tcfree(h->conf);
    tcfree(h->current);
    if(h->titles){
	for(i = 0; i < h->playlist->length; i++)
	    free(h->titles[i]);
	free(h->titles);
    }
    tcfree(h->playlist);
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
    http_reg(remove);
    http_reg(shuffle);
    http_reg(repeat);
    http_reg(loop);
    http_reg(page);

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
