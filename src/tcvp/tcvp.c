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
    eventq_t qs, qt;
    tcconf_section_t *conf;
    int open;
    tchash_table_t *filters;
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

extern int
t_stop(tcvp_module_t *pl, tcvp_event_t *te)
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
do_close(tcvp_player_t *tp)
{
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
	if(s->video.aspect.den)
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

extern int
t_seek(tcvp_module_t *pl, int64_t time, int how)
{
    tcvp_player_t *tp = pl->private;
    uint64_t ntime, stime = -1;
    int s = tp->state;
    int i;

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
	tp->demux->flush(tp->demux, 1);
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
	    tchash_find(tp->filters, id, &pn);

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
		tchash_replace(tp->filters, id, pn);
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
t_open(tcvp_module_t *pl, int nn, char **names)
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

    tp->filters = tchash_new(10, 0);
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

    tchash_destroy(tp->filters, tcfree);

    if(!ns)
	goto err;

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
t_new(tcvp_module_t *tm, tcconf_section_t *cs)
{
    tcvp_player_t *tp;
    char qn[32];
    char qname[16];

    tp = tcallocdz(sizeof(*tp), NULL, t_free);
    tp->state = TCVP_STATE_END;

    sprintf(qname, "TCVP-%u", plc++);
    tcconf_setvalue(cs, "qname", "%s", qname);

    pthread_mutex_init(&tp->tmx, NULL);
    pthread_cond_init(&tp->tcd, NULL);

    tp->qs = eventq_new(NULL);
    sprintf(qn, "%s/status", qname);
    eventq_attach(tp->qs, qn, EVENTQ_SEND);

    tp->qt = eventq_new(NULL);
    sprintf(qn, "%s/timer", qname);
    eventq_attach(tp->qt, qn, EVENTQ_SEND);

    tp->conf = tcref(cs);

    tm->private = tp;
    return 0;
}
