/**
    Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

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
#include <tchash.h>
#include <tcvp_types.h>
#include <tcvp_core_tc2.h>

typedef struct tcvp_player {
    stream_shared_t *ssh;
    tcvp_pipe_t **demux;
    muxed_stream_t **streams;
    int nstreams;
    tcvp_timer_t *timer;
    pthread_t th_ticker, th_event;
    pthread_mutex_t tmx;
    pthread_cond_t tcd;
    int state;
    eventq_t qs, qt;
    tcconf_section_t *conf;
    int open;
    char *outfile;
} tcvp_player_t;

typedef union tcvp_core_event {
    int type;
    tcvp_key_event_t key;
    tcvp_open_event_t open;
    tcvp_open_multi_event_t open_m;
    tcvp_seek_event_t seek;
    tcvp_timer_event_t timer;
    tcvp_state_event_t state;
    tcvp_load_event_t load;
} tcvp_core_event_t;

extern int
t_start(tcvp_module_t *pl, tcvp_event_t *te)
{
    tcvp_player_t *tp = pl->private;
    int i;

    if(tp->demux)
	for(i = 0; i < tp->nstreams; i++)
	    if(tp->demux[i])
		tp->demux[i]->start(tp->demux[i]);

    if(tp->timer)
	tp->timer->start(tp->timer);

    tp->state = TCVP_STATE_PLAYING;
    tcvp_event_send(tp->qs, TCVP_STATE, TCVP_STATE_PLAYING);

    return 0;
}

extern int
t_stop(tcvp_module_t *pl, tcvp_event_t *te)
{
    tcvp_player_t *tp = pl->private;
    int i;

    if(tp->demux)
	for(i = 0; i < tp->nstreams; i++)
	    if(tp->demux[i])
		tp->demux[i]->stop(tp->demux[i]);

    if(tp->timer)
	tp->timer->stop(tp->timer);

    tp->state = TCVP_STATE_STOPPED;
    tcvp_event_send(tp->qs, TCVP_STATE, TCVP_STATE_STOPPED);

    return 0;
}

static int
do_close(tcvp_player_t *tp)
{
    int i;

    pthread_mutex_lock(&tp->tmx);

    if(tp->demux){
	for(i = 0; i < tp->nstreams; i++)
	    if(tp->demux[i])
		tcfree(tp->demux[i]);
	free(tp->demux);
	tp->demux = NULL;
    }

    if(tp->ssh){
	tcfree(tp->ssh);
	tp->ssh = 0;
    }

    if(tp->streams){
	for(i = 0; i < tp->nstreams; i++)
	    if(tp->streams[i])
		tcfree(tp->streams[i]);
	free(tp->streams);
	tp->streams = NULL;
    }

    tp->state = TCVP_STATE_END;
    if(tp->timer)
	tp->timer->interrupt(tp->timer);
    pthread_mutex_unlock(&tp->tmx);

    if(tp->th_ticker){
	pthread_join(tp->th_ticker, NULL);
	tp->th_ticker = 0;
    }

    pthread_mutex_lock(&tp->tmx);

    if(tp->timer){
	tcfree(tp->timer);
	tp->timer = NULL;
    }

    if(tp->outfile){
	free(tp->outfile);
	tp->outfile = NULL;
    }

    tp->open = 0;
    pthread_mutex_unlock(&tp->tmx);

    return 0;
}

extern int
te_close(tcvp_module_t *pl, tcvp_event_t *te)
{
    tcvp_player_t *tp = pl->private;
    do_close(tp);
    return 0;
}

static void
t_free(void *p)
{
    tcvp_player_t *tp = p;

    do_close(tp);

    eventq_delete(tp->qs);
    eventq_delete(tp->qt);
    pthread_mutex_destroy(&tp->tmx);
    pthread_cond_destroy(&tp->tcd);
    tcfree(tp->conf);
}

static void
print_stream(stream_t *s)
{
    printf("%s", s->common.codec);

    switch(s->stream_type){
    case STREAM_TYPE_AUDIO:
	if(s->audio.sample_rate)
	    printf(", %i Hz", s->audio.sample_rate);
	if(s->audio.channels)
	    printf(", %i channels", s->audio.channels);
	break;

    case STREAM_TYPE_VIDEO:
	if(s->video.width)
	    printf(", %ix%i", s->video.width, s->video.height);
	if(s->video.frame_rate.den)
	    printf(", %.2lf fps",
		   (double) s->video.frame_rate.num / s->video.frame_rate.den);
	if(s->video.aspect.num && s->video.aspect.den)
	    printf(", aspect %i/%i (%.2lf)",
		   s->video.aspect.num, s->video.aspect.den,
		   (double) s->video.aspect.num / s->video.aspect.den);
	break;
    }
    if(s->common.bit_rate)
	printf(", %i kb/s", s->audio.bit_rate / 1000);
    printf("\n");
}

static void
print_info(muxed_stream_t *stream)
{
    char *file = tcattr_get(stream, "file");
    char *performer = tcattr_get(stream, "performer");
    char *title = tcattr_get(stream, "title");
    char *album = tcattr_get(stream, "album");
    int i;

    if(file)
	printf("File:      %s\n", file);
    if(performer)
	printf("Performer: %s\n", performer);
    if(album)
	printf("Album:     %s\n", album);
    if(title)
	printf("Title:     %s\n", title);
    if(stream->time)
	printf("Length:    %lli:%02lli\n", stream->time / 27000000 / 60,
	       (stream->time / 27000000) % 60);

    for(i = 0; i < stream->n_streams; i++){
	int u = stream->used_streams[i];
	printf("%2i%*s: ", i, 1 - u, u? "*": "");
	print_stream(stream->streams + i);
#if 0
	if(u){
	    tcvp_pipe_t *np = pipes[ss + i];
	    while(np){
		printf("     ");
		print_stream(&np->format);
		np = np->next;
	    }
	}
#endif
    }
}

static void *
st_ticker(void *p)
{
    tcvp_player_t *tp = p;
    uint64_t time;

    pthread_mutex_lock(&tp->tmx);
    while(tp->state != TCVP_STATE_END){
	time = tp->timer->read(tp->timer);
	tcvp_event_send(tp->qt, TCVP_TIMER, time);
	tp->timer->wait(tp->timer, time + 27000000, &tp->tmx);
    }
    pthread_mutex_unlock(&tp->tmx);

    return NULL;
}

extern int
t_seek(tcvp_module_t *pl, int64_t time, int how)
{
    tcvp_player_t *tp = pl->private;
    uint64_t ntime, stime = -1;
    int s = tp->state;
    int i;

    if(tp->state == TCVP_STATE_END)
	return 0;

    if(s == TCVP_STATE_PLAYING){
	t_stop(pl, NULL);
    }

    if(how == TCVP_SEEK_REL){
	ntime = tp->timer->read(tp->timer);
	if(time < 0 && -time > ntime)
	    ntime = 0;
	else
	    ntime += time;
    } else {
	ntime = time;
    }

    for(i = 0; i < tp->nstreams; i++){
	if(tp->streams[i]->seek){
	    uint64_t nt = tp->streams[i]->seek(tp->streams[i], ntime);
	    if(nt < stime)
		stime = nt;
	}
    }

    if(stime != -1LL){
	pthread_mutex_lock(&tp->tmx);
	for(i = 0; i < tp->nstreams; i++)
	    tp->demux[i]->flush(tp->demux[i], 1);
	tp->timer->reset(tp->timer, stime);
	tp->timer->interrupt(tp->timer);
	pthread_mutex_unlock(&tp->tmx);
    }

    if(s == TCVP_STATE_PLAYING){
	t_start(pl, NULL);
    }

    return 0;    
}

extern int
te_seek(tcvp_module_t *pl, tcvp_event_t *te)
{
    tcvp_seek_event_t *se = (tcvp_seek_event_t *) te;
    return t_seek(pl, se->time, se->how);
}

static char *
exp_stream(char *n, void *p)
{
    char *v = tcattr_get(p, n);

    if(!v){
	if(!strcmp(n, "artist"))
	    v = tcattr_get(p, "performer");
	else if(!strcmp(n, "performer"))
	    v = tcattr_get(p, "artist");
    }

    return v;
}

static int
open_files(tcvp_player_t *tp, int n, char **files, tcconf_section_t *cs)
{
    int i;

    tp->streams = calloc(n, sizeof(*tp->streams));
    tp->nstreams = 0;

    for(i = 0; i < n; i++){
	tc2_print("TCVP", TC2_PRINT_VERBOSE,
		  "opening file '%s'\n", files[i]);
	if((tp->streams[tp->nstreams] = stream_open(files[i], cs, tp->timer))){
	    tp->nstreams++;
	}
    }

    tc2_print("TCVP", TC2_PRINT_VERBOSE, "%i files opened\n", tp->nstreams);

    return tp->nstreams;
}

static int
t_open(tcvp_module_t *pl, int nn, char **names)
{
    tcvp_player_t *tp = pl->private;
    int ns = 0;
    char *profile = strdup(tcvp_conf_default_profile), prname[256];
    tcconf_section_t *prsec, *dc;
    uint64_t start_time = 0;
    char *outfile;
    int start;
    int i, j;

    if(tp->conf)
	tcconf_getvalue(tp->conf, "profile", "%s", &profile);

    snprintf(prname, 256, "TCVP/profiles/%s", profile);
    if(!(prsec = tc2_get_conf(prname))){
	tc2_print("TCVP", TC2_PRINT_ERROR, "No profile '%s'\n", profile);
	return -1;
    }

    free(profile);

    dc = tcconf_getsection(prsec, "demux");
    if(dc){
	tcconf_section_t *nc = tcconf_merge(NULL, dc);
	tcfree(dc);
	dc = nc;
    }
    dc = tcconf_merge(dc, tp->conf);

    tp->timer = timer_new(tp->conf);
    open_files(tp, nn, names, dc);

    tcfree(dc);

    if(tp->nstreams <= 0){
	return -1;
    }

    for(i = 0; i < tp->nstreams; i++){
	void *as = NULL;
	char *at, *av;
	while(tcconf_nextvalue(tp->conf, "attr", &as, "%s%s", &at, &av) > 0){
	    char *t = tcstrexp(av, "{", "}", ':', exp_stream,
			       tp->streams[i], TCSTREXP_ESCAPE);
	    tcattr_set(tp->streams[i], at, t, NULL, free);
	    free(at);
	    free(av);
	}
	at = tcattr_get(tp->streams[i], "artist");
	if(at)
	    tcattr_set(tp->streams[i], "performer", at, NULL, NULL);
    }

    if(tcconf_getvalue(tp->conf, "outname", "%s", &outfile) > 0 ||
       tcconf_getvalue(prsec, "outname", "%s", &outfile) > 0){
	tp->outfile = tcstrexp(outfile, "{", "}", ':', exp_stream,
			       tp->streams[0], TCSTREXP_ESCAPE);
	free(outfile);
    }

    tp->open = 1;

    if(!tp->timer->have_driver){
	int tres = 10;
	char *tdrv = NULL;
	timer_driver_t *td;
	driver_timer_new_t dtn;
	tcconf_section_t *tcf, *mtc = NULL;

	tcf = tcconf_getsection(prsec, "timer");
	if(tcf)
	    mtc = tcconf_merge(NULL, tcf);
	mtc = tcconf_merge(mtc, tp->conf);

	tcconf_getvalue(mtc, "resolution", "%i", &tres);
	tcconf_getvalue(mtc, "driver", "%s", &tdrv);
	if(tdrv && (dtn = tc2_get_symbol(tdrv, "new")))
	    td = dtn(mtc, tres);
	else
	    td = driver_timer_new(mtc, tres);
	tres *= 27000;
	tp->timer->set_driver(tp->timer, td);
	if(tcf)
	    tcfree(tcf);
	tcfree(mtc);
    }

    tp->ssh = stream_new(prsec, tp->conf, tp->timer, tp->outfile);
    tp->demux = calloc(tp->nstreams, sizeof(*tp->demux));

    tcfree(prsec);
    prsec = NULL;

    for(i = 0, j = 0; i < tp->nstreams; i++){
	tp->demux[i] = stream_play(tp->ssh, tp->streams[i]);
	ns += !!tp->demux[i];
	if(tcvp_conf_verbose)
	    print_info(tp->streams[i]);
    }

    tp->state = TCVP_STATE_STOPPED;

    if(ns <= 0)
	goto err;

    if(tcconf_getvalue(tp->conf, "start_time", "%i", &start) == 1){
	start_time = (uint64_t) start * 27000000LL;
	t_seek(pl, start_time, TCVP_SEEK_ABS);
    }

    pthread_create(&tp->th_ticker, NULL, st_ticker, tp);

    for(i = 0; i < tp->nstreams; i++){
	tp->demux[i]->start(tp->demux[i]);
    }

    return 0;

err:
    printf("No supported streams found.\n");
    for(i = 0; i < tp->nstreams; i++)
	tcfree(tp->streams[i]);
    free(tp->streams);
    tp->streams = NULL;
    if(prsec)
	tcfree(prsec);
    return -1;
}

extern int
te_open(tcvp_module_t *tm, tcvp_event_t *e)
{
    tcvp_player_t *tp = tm->private;
    tcvp_core_event_t *te = (tcvp_core_event_t *) e;
    if(t_open(tm, 1, &te->open.file) < 0)
	tcvp_event_send(tp->qs, TCVP_STATE, TCVP_STATE_ERROR);
    return 0;
}

extern int
te_openm(tcvp_module_t *tm, tcvp_event_t *e)
{
    tcvp_player_t *tp = tm->private;
    tcvp_core_event_t *te = (tcvp_core_event_t *) e;
    if(t_open(tm, te->open_m.nfiles, te->open_m.files) < 0)
	tcvp_event_send(tp->qs, TCVP_STATE, TCVP_STATE_ERROR);
    return 0;
}

extern int
te_pause(tcvp_module_t *tm, tcvp_event_t *e)
{
    tcvp_player_t *tp = tm->private;
    if(tp->state == TCVP_STATE_PLAYING)
	t_stop(tm, NULL);
    else if(tp->state == TCVP_STATE_STOPPED)
	t_start(tm, NULL);
    return 0;
}

extern int
te_query(tcvp_module_t *tm, tcvp_event_t *e)
{
    tcvp_player_t *tp = tm->private;
    tcvp_event_send(tp->qs, TCVP_STATE, tp->state);
    if(tp->streams && tp->streams[0])
	tcvp_event_send(tp->qs, TCVP_LOAD, tp->streams[0]); /* FIXME */
    if(tp->timer)
	tcvp_event_send(tp->qt, TCVP_TIMER, tp->timer->read(tp->timer));
    return 0;
}

static u_int plc;

extern int
t_init(tcvp_module_t *tm)
{
    tcvp_player_t *tp = tm->private;
    char *qname, qn[32];

    if(!tcconf_getvalue(tp->conf, "features/core", ""))
	return -1;

    tcconf_getvalue(tp->conf, "qname", "%s", &qname);
    sprintf(qn, "%s/status", qname);
    eventq_attach(tp->qs, qn, EVENTQ_SEND);

    sprintf(qn, "%s/timer", qname);
    eventq_attach(tp->qt, qn, EVENTQ_SEND);

    tcconf_setvalue(tp->conf, "features/core", "");

    free(qname);
    return 0;
}

extern int
t_new(tcvp_module_t *tm, tcconf_section_t *cs)
{
    tcvp_player_t *tp;
    char *qname;

    tp = tcallocdz(sizeof(*tp), NULL, t_free);
    tp->state = TCVP_STATE_END;

    if(tcconf_getvalue(cs, "qname", "%s", &qname) < 1){
	qname = malloc(16);
	sprintf(qname, "TCVP-%u", plc++);
	tcconf_setvalue(cs, "qname", "%s", qname);
    }

    tp->qs = eventq_new(NULL);
    tp->qt = eventq_new(NULL);

    pthread_mutex_init(&tp->tmx, NULL);
    pthread_cond_init(&tp->tcd, NULL);

    tp->conf = tcref(cs);
    tm->private = tp;

    free(qname);
    return 0;
}
