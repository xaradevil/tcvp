/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
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
    tcvp_pipe_t *demux, **pipes, **ends;
    int npipes;
    muxed_stream_t **streams;
    int nstreams;
    tcvp_timer_t *timer;
    pthread_t th_ticker, th_event;
    pthread_mutex_t tmx;
    pthread_cond_t tcd;
    int state;
    eventq_t qs, qr, qt;
    tcconf_section_t *conf;
    int open;
    hash_table *filters;
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

static int TCVP_OPEN;
static int TCVP_OPEN_MULTI;
static int TCVP_START;
static int TCVP_STOP;
static int TCVP_PAUSE;
static int TCVP_SEEK;
static int TCVP_CLOSE;
static int TCVP_STATE;
static int TCVP_TIMER;
static int TCVP_LOAD;
static int TCVP_QUERY;

static int
t_start(player_t *pl)
{
    tcvp_player_t *tp = pl->private;
    int i;

    if(tp->demux)
	tp->demux->start(tp->demux);

    if(tp->ends)
	for(i = 0; i < tp->npipes; i++)
	    if(tp->ends[i] && tp->ends[i]->start)
		tp->ends[i]->start(tp->ends[i]);

    if(tp->timer)
	tp->timer->start(tp->timer);

    tp->state = TCVP_STATE_PLAYING;
    tcvp_event_send(tp->qs, TCVP_STATE, TCVP_STATE_PLAYING);

    return 0;
}

static int
t_stop(player_t *pl)
{
    tcvp_player_t *tp = pl->private;
    int i;

    if(tp->demux)
	tp->demux->stop(tp->demux);

    if(tp->ends)
	for(i = 0; i < tp->npipes; i++)
	    if(tp->ends[i] && tp->ends[i]->stop)
		tp->ends[i]->stop(tp->ends[i]);

    if(tp->timer)
	tp->timer->stop(tp->timer);

    tp->state = TCVP_STATE_STOPPED;
    tcvp_event_send(tp->qs, TCVP_STATE, TCVP_STATE_STOPPED);

    return 0;
}

static void
close_pipe(tcvp_pipe_t *p)
{
    while(p){
	tcvp_pipe_t *np = p->next;
	tcfree(p);
	p = np;
    }
}

static int
t_close(player_t *pl)
{
    tcvp_player_t *tp = pl->private;
    int i;

    pthread_mutex_lock(&tp->tmx);

    if(tp->demux){
	tcfree(tp->demux);
	tp->demux = NULL;
    }

    if(tp->pipes){
	for(i = 0; i < tp->npipes; i++)
	    close_pipe(tp->pipes[i]);
	free(tp->pipes);
	tp->npipes = 0;
	tp->pipes = NULL;
    }

    if(tp->ends){
	free(tp->ends);
	tp->ends = NULL;
    }

    if(tp->streams){
	for(i = 0; i < tp->nstreams; i++)
	    if(tp->streams[i])
		tcfree(tp->streams[i]);
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

static void
t_free(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    tcvp_event_send(tp->qr, -1);
    pthread_join(tp->th_event, NULL);

    t_close(pl);

    eventq_delete(tp->qr);
    eventq_delete(tp->qs);
    eventq_delete(tp->qt);
    pthread_mutex_destroy(&tp->tmx);
    pthread_cond_destroy(&tp->tcd);
    tcfree(tp->conf);

    free(tp);
    free(pl);
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
	    printf(", %lf fps",
		   (double) s->video.frame_rate.num / s->video.frame_rate.den);
	if(s->video.aspect.den)
	    printf(", aspect %lf",
		   (double) s->video.aspect.num / s->video.aspect.den);
	break;
    }
    if(s->common.bit_rate)
	printf(", %i kb/s", s->audio.bit_rate / 1000);
    printf("\n");
}

static void
print_info(muxed_stream_t *stream, tcvp_pipe_t **pipes, int ss)
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
	printf("%2i%*s: ", ss + i, 1 - u, u? "*": "");
	print_stream(stream->streams + i);
	if(u){
	    tcvp_pipe_t *np = pipes[ss + i];
	    while(np){
		printf("     ");
		print_stream(&np->format);
		np = np->next;
	    }
	}
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

static int
t_seek(player_t *pl, int64_t time, int how)
{
    tcvp_player_t *tp = pl->private;
    uint64_t ntime, stime = -1;
    int s = tp->state;
    int i;

    if(s == TCVP_STATE_PLAYING){
	t_stop(pl);
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
	tp->demux->flush(tp->demux, 1);
	tp->timer->reset(tp->timer, stime);
	tp->timer->interrupt(tp->timer);
	pthread_mutex_unlock(&tp->tmx);
    }

    if(s == TCVP_STATE_PLAYING){
	t_start(pl);
    }

    return 0;    
}

static tcvp_pipe_t *
new_pipe(tcvp_player_t *tp, stream_t *s, tcconf_section_t *p,
	 muxed_stream_t *ms)
{
    tcvp_pipe_t *pipe = NULL, *pp = NULL, *pn = NULL;
    tcconf_section_t *f, *mcf;
    void *cs = NULL;

    while((f = tcconf_nextsection(p, "filter", &cs))){
	char *type, *id = NULL;
	filter_new_t fn;

	pn = NULL;

	if(tcconf_getvalue(f, "type", "%s", &type) < 1)
	    continue;

	if(tcconf_getvalue(f, "id", "%s", &id) > 0)
	    hash_find(tp->filters, id, &pn);

	if(!pn){
	    if(!(fn = tc2_get_symbol(type, "new")))
		break;

	    mcf = tcconf_merge(NULL, f);
	    tcconf_merge(mcf, tp->conf);
	    if(tp->outfile)
		tcconf_setvalue(mcf, "mux/url", "%s", tp->outfile);

	    if(!(pn = fn(pp? &pp->format: s, mcf, tp->timer, ms)))
		break;

	    if(id)
		hash_replace(tp->filters, id, pn);
	    tcfree(mcf);
	}

	if(id){
	    tcref(pn);
	    free(id);
	}

	if(!pipe)
	    pipe = pn;

	if(pp)
	    pp->next = pn;
	pp = pn;
	free(type);
	tcfree(f);
    }

    if(!pn){
	close_pipe(pipe);
	pipe = NULL;
    }

    return pipe;
}

static tcvp_pipe_t *
pipe_end(tcvp_pipe_t *p)
{
    while(p && p->next)
	p = p->next;
    return p;
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
    tp->npipes = 0;

    for(i = 0; i < n; i++){
	if((tp->streams[tp->nstreams] = stream_open(files[i], cs, tp->timer))){
	    tp->npipes += tp->streams[tp->nstreams]->n_streams;
	    tp->nstreams++;
	}
    }

    return tp->nstreams;
}

static void
stream_time(muxed_stream_t *stream, tcvp_pipe_t **pipes)
{
    int i;

    if(!stream->time){
	for(i = 0; i < stream->n_streams; i++){
	    if(stream->used_streams[i]){
		tcvp_pipe_t *p = pipes[i];
		uint64_t len = 0;
		while(p){
		    stream_t *st = &p->format;
		    if(st->stream_type == STREAM_TYPE_VIDEO){
			int frames = st->video.frames;
			int frn = st->video.frame_rate.num;
			int frd = st->video.frame_rate.den;
			if(frn > 0 && frd > 0 && frames > 0){
			    len = (uint64_t) frames * 27000000LL * frd / frn;
			    break;
			}
		    } else if(st->stream_type == STREAM_TYPE_AUDIO){
			int samples = st->audio.samples;
			int srate = st->audio.sample_rate;
			if(srate > 0 && samples > 0){
			    len = (uint64_t) samples * 27000000LL / srate;
			    break;
			}
		    }
		    p = p->next;
		}
		if(len > stream->time){
		    stream->time = len;
		}
	    }
	}
    }
}

static int
t_open(player_t *pl, int nn, char **names)
{
    tcvp_pipe_t *demux = NULL;
    tcvp_player_t *tp = pl->private;
    int ns = 0, *us, vs = -1, as = -1;
    char *profile = strdup(tcvp_conf_default_profile), prname[256];
    tcconf_section_t *prsec, *dc;
    uint64_t start_time = -1;
    struct { muxed_stream_t *ms; int *used; } *mstreams;
    stream_t **streams;
    char *outfile;
    int start;
    int i, j;

    if(tp->conf)
	tcconf_getvalue(tp->conf, "profile", "%s", &profile);

    snprintf(prname, 256, "TCVP/profiles/%s", profile);
    if(!(prsec = tc2_get_conf(prname))){
	fprintf(stderr, "TCVP: No profile '%s'\n", profile);
	return -1;
    }

    tp->filters = hash_new(10, 0);
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

    if(tcconf_getvalue(tp->conf, "outname", "%s", &outfile) > 0 ||
       tcconf_getvalue(prsec, "outname", "%s", &outfile) > 0){
	tp->outfile = tcstrexp(outfile, "{", "}", ':', exp_stream,
			       tp->streams[0], TCSTREXP_ESCAPE);
	free(outfile);
    }

    us = alloca(tp->npipes * sizeof(*us));
    memset(us, 0, tp->npipes * sizeof(*us));
    streams = alloca(tp->npipes * sizeof(*streams));
    mstreams = alloca(tp->npipes * sizeof(*mstreams));

    for(i = 0, j = 0; i < tp->nstreams; i++){
	int k;
	for(k = 0; k < tp->streams[i]->n_streams; j++, k++){
	    streams[j] = tp->streams[i]->streams + k;
	    mstreams[j].ms = tp->streams[i];
	    mstreams[j].used = tp->streams[i]->used_streams + k;
	}
    }

    if(tp->conf){
	void *cs = NULL;
	int s;
	i = 0;
	while(tcconf_nextvalue(tp->conf, "video/stream", &cs, "%i", &s) > 0){
	    if(s >= 0 && s < tp->npipes &&
	       streams[s]->stream_type == STREAM_TYPE_VIDEO){
		us[s] = 1;
		vs = s;
	    } else {
		vs = -2;
	    }
	}
	while(tcconf_nextvalue(tp->conf, "audio/stream", &cs, "%i", &s) > 0){
	    if(s >= 0 && s < tp->npipes &&
	       streams[s]->stream_type == STREAM_TYPE_AUDIO){
		us[s] = 1;
		as = s;
	    } else {
		as = -2;
	    }
	}
    }

    tp->pipes = calloc(tp->npipes, sizeof(*tp->pipes));

    for(i = 0; i < tp->npipes; i++){
	stream_t *st = streams[i];
	tcconf_section_t *pc;

	if(st->stream_type == STREAM_TYPE_VIDEO &&
	   (us[i] || (vs > -2 && vs < 0))){
	    if((pc = tcconf_getsection(prsec, "video"))){
		if((tp->pipes[i] = new_pipe(tp, st, pc, mstreams[i].ms))){
		    *mstreams[i].used = 1;
		    ns++;
		    vs = i;
		}
		tcfree(pc);
	    }
	} else if(st->stream_type == STREAM_TYPE_AUDIO &&
		  (us[i] || (as > -2 && as < 0))){
	    if((pc = tcconf_getsection(prsec, "audio"))){
		if((tp->pipes[i] = new_pipe(tp, st, pc, mstreams[i].ms))){
		    *mstreams[i].used = 1;
		    ns++;
		    as = i;
		}
		tcfree(pc);
	    }
	}
    }

    hash_destroy(tp->filters, tcfree);

    if(!ns)
	goto err;

    tp->open = 1;

    if(!tp->timer->have_driver){
	int tres = 10;
	tcconf_getvalue(prsec, "timer/resolution", "%i", &tres);
	tres *= 27000;
	tp->timer->set_driver(tp->timer, driver_timer_new(tres));
    }

    tcfree(prsec);
    prsec = NULL;

    demux = stream_play(tp->streams, tp->nstreams, tp->pipes, tp->conf);

    for(i = 0; i < tp->npipes; i++){
	if(streams[i]->common.start_time < start_time)
	    start_time = streams[i]->common.start_time;
    }

    for(i = 0, j = 0; i < tp->nstreams; i++){
	stream_time(tp->streams[i], tp->pipes + j);
	if(tcvp_conf_verbose)
	    print_info(tp->streams[i], tp->pipes, j);
	j += tp->streams[i]->n_streams;
    }

    tp->state = TCVP_STATE_STOPPED;
    tp->ends = calloc(tp->npipes, sizeof(*tp->ends));
    for(i = 0; i < tp->npipes; i++){
	if(tp->pipes[i]){
	    if(*mstreams[i].used){
		tp->ends[i] = pipe_end(tp->pipes[i]);
	    } else {
		close_pipe(tp->pipes[i]);
		tp->pipes[i] = NULL;
		ns--;
	    }
	}
    }

    if(ns <= 0)
	goto err;

    tp->demux = demux;

    if(tcconf_getvalue(tp->conf, "start_time", "%i", &start) == 1){
	start_time = (uint64_t) start * 27000000LL;
	t_seek(pl, start_time, TCVP_SEEK_ABS);
    }

    tp->timer->reset(tp->timer, start_time);

    pthread_create(&tp->th_ticker, NULL, st_ticker, tp);

    demux->start(demux);
    for(i = 0; i < tp->npipes; i++)
	if(tp->ends[i] && tp->ends[i]->buffer)
	    tp->ends[i]->buffer(tp->ends[i], 0.9);

    for(i = 0; i < tp->nstreams; i++){
	tcvp_event_send(tp->qs, TCVP_LOAD, tp->streams[i]);
    }

    return 0;

err:
    printf("No supported streams found.\n");
    if(demux)
	tcfree(demux);
    for(i = 0; i < tp->nstreams; i++)
	tcfree(tp->streams[i]);
    free(tp->streams);
    tp->streams = NULL;
    if(prsec)
	tcfree(prsec);
    free(tp->pipes);
    tp->pipes = NULL;
    if(tp->ends){
	free(tp->ends);
	tp->ends = NULL;
    }
    return -1;
}

static void *
t_event(void *p)
{
    player_t *pl = p;
    tcvp_player_t *tp = pl->private;
    int r = 1;

    while(r){
	tcvp_core_event_t *te = eventq_recv(tp->qr);

	if(te->type == TCVP_OPEN){
	    if(t_open(pl, 1, &te->open.file) < 0)
		tcvp_event_send(tp->qs, TCVP_STATE, TCVP_STATE_ERROR);
	} else if(te->type == TCVP_OPEN_MULTI){
	    if(t_open(pl, te->open_m.nfiles, te->open_m.files) < 0)
		tcvp_event_send(tp->qs, TCVP_STATE, TCVP_STATE_ERROR);
	} else if(te->type == TCVP_START){
	    t_start(pl);
	} else if(te->type == TCVP_STOP){
	    t_stop(pl);
	} else if(te->type == TCVP_PAUSE){
	    if(tp->state == TCVP_STATE_PLAYING)
		t_stop(pl);
	    else if(tp->state == TCVP_STATE_STOPPED)
		t_start(pl);
	} else if(te->type == TCVP_SEEK){
	    t_seek(pl, te->seek.time, te->seek.how);
	} else if(te->type == TCVP_CLOSE){
	    t_close(pl);
	} else if(te->type == TCVP_QUERY){
	    tcvp_event_send(tp->qs, TCVP_STATE, tp->state);
	    if(tp->streams && tp->streams[0])
		tcvp_event_send(tp->qs, TCVP_LOAD, tp->streams[0]); /* FIXME */
	    if(tp->timer)
		tcvp_event_send(tp->qt, TCVP_TIMER,
				tp->timer->read(tp->timer));
	} else if(te->type == -1){
	    r = 0;
	}
	tcfree(te);
    }
    return NULL;
}

static int
q_cmd(player_t *pl, int cmd)
{
    tcvp_player_t *tp = pl->private;
    tcvp_event_send(tp->qr, cmd);
    return 0;
}

static int
q_start(player_t *pl)
{
    return q_cmd(pl, TCVP_START);
}

static int
q_stop(player_t *pl)
{
    return q_cmd(pl, TCVP_STOP);
}

static int
q_close(player_t *pl)
{
    return q_cmd(pl, TCVP_CLOSE);
}

static int
q_seek(player_t *pl, uint64_t pts)
{
    tcvp_player_t *tp = pl->private;
    tcvp_event_send(tp->qr, TCVP_SEEK, pts, TCVP_SEEK_ABS);
    return 0;
}

static u_int plc;

extern player_t *
t_new(tcconf_section_t *cs)
{
    tcvp_player_t *tp;
    player_t *pl;
    char qn[32];
    char qname[16];

    tp = calloc(1, sizeof(*tp));
    pl = calloc(1, sizeof(*pl));
    pl->start = q_start;
    pl->stop = q_stop;
    pl->seek = q_seek;
    pl->close = q_close;
    pl->free = t_free;
    pl->private = tp;

    sprintf(qname, "TCVP-%u", plc++);
    tcconf_setvalue(cs, "qname", "%s", qname);

    pthread_mutex_init(&tp->tmx, NULL);
    pthread_cond_init(&tp->tcd, NULL);

    tp->qs = eventq_new(NULL);
    sprintf(qn, "%s/status", qname);
    eventq_attach(tp->qs, qn, EVENTQ_SEND);

    tp->qr = eventq_new(tcref);
    sprintf(qn, "%s/control", qname);
    eventq_attach(tp->qr, qn, EVENTQ_RECV);

    tp->qt = eventq_new(NULL);
    sprintf(qn, "%s/timer", qname);
    eventq_attach(tp->qt, qn, EVENTQ_SEND);

    pthread_create(&tp->th_event, NULL, t_event, pl);
    tp->conf = tcref(cs);

    return pl;
}

extern int
init_core(void)
{
    TCVP_OPEN = tcvp_event_get("TCVP_OPEN"); 
    TCVP_OPEN_MULTI = tcvp_event_get("TCVP_OPEN_MULTI"); 
    TCVP_START = tcvp_event_get("TCVP_START");
    TCVP_STOP = tcvp_event_get("TCVP_STOP"); 
    TCVP_PAUSE = tcvp_event_get("TCVP_PAUSE");
    TCVP_SEEK = tcvp_event_get("TCVP_SEEK"); 
    TCVP_CLOSE = tcvp_event_get("TCVP_CLOSE");
    TCVP_STATE = tcvp_event_get("TCVP_STATE");
    TCVP_TIMER = tcvp_event_get("TCVP_TIMER");
    TCVP_LOAD = tcvp_event_get("TCVP_LOAD");
    TCVP_QUERY = tcvp_event_get("TCVP_QUERY");

    return 0;
}
