/**
    Copyright (C) 2004  Michael Ahlberg, Måns Rullgård

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

#define TCHASH_NEWAPI

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <pthread.h>
#include <semaphore.h>
#include <tcalloc.h>
#include <tclist.h>
#include <tchash.h>
#include <tcvp_types.h>
#include <stream_tc2.h>

#define RUN   1
#define STOP  2
#define PAUSE 3

#define min_buffer tcvp_demux_stream_conf_buffer

struct stream_shared {
    int sid;
    int as, vs;
    uint64_t starttime, endtime, playtime;
    tcconf_section_t *conf, *profile;
    tchash_table_t *filters;
    tcvp_timer_t *timer;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    eventq_t sq;
    char *outfile;
    int nstreams;
};

typedef struct stream_play {
    muxed_stream_t *ms;
    struct sp_stream {
	tcvp_pipe_t *pipe, *end;
	tclist_t *packets;
	uint64_t starttime;
	int probe;
	pthread_t th;
	struct stream_play *sp;
    } *streams;
    int *smap;
    int nstreams, pstreams;
    int waiting;
    int nbuf;
    int state;
    pthread_t rth;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    stream_shared_t *shared;
} stream_player_t;

static void
stream_time(muxed_stream_t *stream, int i, tcvp_pipe_t *pipe)
{
    if(!stream->time){
	tcvp_pipe_t *p = pipe;
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

static void
close_pipe(tcvp_pipe_t *p)
{
    while(p){
	tcvp_pipe_t *np = p->next;
	tcfree(p);
	p = np;
    }
}

static tcvp_pipe_t *
new_pipe(stream_player_t *sp, stream_t *s)
{
    tcvp_pipe_t *pipe = NULL, *pp = NULL, *pn = NULL;
    tcconf_section_t *f, *mcf;
    stream_shared_t *sh = sp->shared;
    tcconf_section_t *pr = NULL;
    void *cs = NULL;

    switch(s->stream_type){
    case STREAM_TYPE_VIDEO:
	pr = tcconf_getsection(sh->profile, "video");
	break;
    case STREAM_TYPE_AUDIO:
	pr = tcconf_getsection(sh->profile, "audio");
	break;
    }

    if(!pr)
	return NULL;

    while((f = tcconf_nextsection(pr, "filter", &cs))){
	char *type, *id = NULL;
	filter_new_t fn;

	pn = NULL;

	if(tcconf_getvalue(f, "type", "%s", &type) < 1){
	    tc2_print("STREAM", TC2_PRINT_WARNING,
		      "bad filter specification\n");
	    continue;
	}

	if(tcconf_getvalue(f, "id", "%s", &id) > 0)
	    tchash_find(sh->filters, id, -1, &pn);

	if(!pn){
	    tc2_print("STREAM", TC2_PRINT_DEBUG,
		      "opening new filter: %s\n", type);
	    if(!(fn = tc2_get_symbol(type, "new")))
		break;

	    mcf = tcconf_merge(NULL, f);
	    tcconf_merge(mcf, sh->conf);
	    if(sh->outfile)
		tcconf_setvalue(mcf, "mux/url", "%s", sh->outfile);

	    if(!(pn = fn(pp? &pp->format: s, mcf, sh->timer, sp->ms))){
		tc2_print("STREAM", TC2_PRINT_WARNING,
			  "error opening filter %s\n", type);
		break;
	    }

	    if(id)
		tchash_replace(sh->filters, id, -1, pn, NULL);
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

	if(pp->next){
	    while((pp = pp->next))
		tcref(pp);
	    break;
	}
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

static int
add_stream(stream_player_t *sp, int s)
{
    stream_shared_t *sh = sp->shared;
    tcvp_pipe_t *tp;
    int sid;
    int r = -1;

    pthread_mutex_lock(&sh->lock);
    sid = sh->sid++;
    sp->ms->used_streams[s] = 0;

    tc2_print("STREAM", TC2_PRINT_DEBUG, "new stream #%i\n", sid);

    if(s >= sp->nstreams){
	sp->streams = realloc(sp->streams, (s + 1) * sizeof(*sp->streams));
	memset(sp->streams + s, 0, (s+1-sp->nstreams) * sizeof(*sp->streams));
	sp->smap = realloc(sp->smap, (s + 1) * sizeof(*sp->smap));
	memset(sp->smap + s, 0xff, (s + 1 - sp->nstreams) * sizeof(*sp->smap));
	sp->nstreams = s + 1;
    }

    sp->smap[s] = sid;

    if(sp->ms->streams[s].stream_type == STREAM_TYPE_VIDEO){
	if(sh->vs > -1)
	    goto out;
	sh->vs = sid;
    } else if(sp->ms->streams[s].stream_type == STREAM_TYPE_AUDIO){
	if(sh->as > -1){
	    tc2_print("STREAM", TC2_PRINT_DEBUG,
		      "audio stream present as #%i, skipping\n", sh->as);
	    goto out;
	}
	sh->as = sid;
    }

    tc2_print("STREAM", TC2_PRINT_DEBUG,
	      "creating pipeline for stream #%i\n", sid);
    if(!(tp = new_pipe(sp, sp->ms->streams + s)))
	goto out;

    pthread_mutex_lock(&sp->lock);
    sp->pstreams++;
    pthread_mutex_unlock(&sp->lock);

    sp->streams[s].pipe = tp;
    sp->streams[s].end = pipe_end(tp);
    sp->streams[s].packets = tclist_new(TC_LOCK_NONE);
    sp->streams[s].probe = PROBE_AGAIN;
    sp->streams[s].sp = sp;
    sp->streams[s].starttime = -1LL;

    sp->ms->used_streams[s] = 1;
    sp->nbuf |= 1 << s;

    r = 0;

out:
    pthread_mutex_unlock(&sh->lock);
    if(r)
	tc2_print("STREAM", TC2_PRINT_WARNING,
		  "error opening stream #%i\n", sid);
    return r;
}

static int
del_stream(stream_player_t *sp, int s)
{
    stream_shared_t *sh = sp->shared;
    int ss = sp->smap[s];
    struct sp_stream *str = sp->streams + s;

    tc2_print("STREAM", TC2_PRINT_DEBUG, "deleting stream %i\n", s);

    if(ss == sh->vs)
	sh->vs = -1;
    else if(ss == sh->as)
	sh->as = -1;

    pthread_mutex_lock(&sp->lock);

    close_pipe(str->pipe);
    str->pipe = NULL;
    str->end = NULL;

    if(str->packets){
	tclist_destroy(str->packets, tcfree);
	str->packets = NULL;
    }

    sp->ms->used_streams[s] = 0;

    if(!--sp->pstreams){
	pthread_mutex_lock(&sh->lock);
	if(!--sh->nstreams)
	    tcvp_event_send(sh->sq, TCVP_STATE, TCVP_STATE_END);
	pthread_mutex_unlock(&sh->lock);
    }

    pthread_cond_broadcast(&sp->cond);
    pthread_mutex_unlock(&sp->lock);

    return 0;
}

static int
waitplay(stream_player_t *sp, int s)
{
    struct sp_stream *sps = sp->streams + s;
    int w = 1;

    pthread_mutex_lock(&sp->lock);
    while((!tclist_items(sps->packets) || sp->state == PAUSE) &&
	  sp->state != STOP){
	if(w){
	    sp->waiting += w;
	    pthread_cond_broadcast(&sp->cond);
	    w = 0;
	}
	pthread_cond_wait(&sp->cond, &sp->lock);
    }

    if(!w){
	sp->waiting--;
	pthread_cond_broadcast(&sp->cond);
    }
    pthread_mutex_unlock(&sp->lock);

    return sp->state != STOP;
}

static void *
play_stream(void *p)
{
    struct sp_stream *str = p;
    stream_player_t *sp = str->sp;
    int six = str - sp->streams;
    int shs = sp->smap[six];
    packet_t *pk;

    tc2_print("STREAM", TC2_PRINT_DEBUG,
	      "starting player thread for stream %i\n", shs);

    while(waitplay(sp, six)){
	pthread_mutex_lock(&sp->lock);
	pk = tclist_shift(str->packets);
	if(tclist_items(str->packets) < min_buffer){
	    sp->nbuf |= 1 << six;
	    pthread_cond_broadcast(&sp->cond);
	}
	pthread_mutex_unlock(&sp->lock);
	if(!pk){
	    tc2_print("STREAM", TC2_PRINT_DEBUG,
		      "null packet on stream %i\n", shs);
	    break;
	}

	if(str->pipe->input(str->pipe, pk)){
	    tc2_print("STREAM", TC2_PRINT_ERROR,
		      "stream %i pipeline error\n", shs);
	    break;
	}
    }

    tc2_print("STREAM", TC2_PRINT_DEBUG,
	      "stream %i %s\n", shs, sp->state == STOP? "stopped": "end");

    pk = tcallocz(sizeof(*pk));
    pk->stream = shs;
    pk->data = NULL;
    if(str->end->start)
	str->end->start(str->end);
    str->pipe->input(str->pipe, pk);
    str->pipe->flush(str->pipe, sp->state == STOP);

    del_stream(sp, six);

    return NULL;
}

static int
waitbuf(stream_player_t *sp)
{
    pthread_mutex_lock(&sp->lock);
    while((!sp->nbuf || sp->state == PAUSE) && sp->state != STOP)
	pthread_cond_wait(&sp->cond, &sp->lock);
    pthread_mutex_unlock(&sp->lock);

    return sp->state != STOP;
}

static void *
read_stream(void *p)
{
    stream_player_t *sp = p;
    stream_shared_t *sh = sp->shared;

    while(waitbuf(sp)){
	packet_t *pk = sp->ms->next_packet(sp->ms, -1);
	struct sp_stream *str;
	int ps;

	if(!pk){
	    int i;
	    pthread_mutex_lock(&sp->lock);
	    for(i = 0; i < sp->nstreams; i++)
		if(sp->streams[i].packets)
		    tclist_push(sp->streams[i].packets, NULL);
	    pthread_cond_broadcast(&sp->cond);
	    pthread_mutex_unlock(&sp->lock);
	    break;
	}

	ps = pk->stream;

	if(pk->stream >= sp->nstreams || sp->smap[ps] < 0){
	    if(add_stream(sp, ps)){
		del_stream(sp, ps);
		continue;
	    }
	}

	str = sp->streams + ps;
	pk->stream = sp->smap[ps];

	switch(pk->type){
	case TCVP_PKT_TYPE_DATA:
	    pthread_mutex_lock(&sh->lock);
	    if(pk->flags & TCVP_PKT_FLAG_PTS && sh->starttime == -1LL){
		sh->starttime = pk->pts;
		if(sh->playtime != -1LL)
		    sh->endtime = sh->starttime + sh->playtime;
		sh->timer->reset(sh->timer, sh->starttime);
		tc2_print("STREAM", TC2_PRINT_DEBUG, "start %llu, end %llu\n",
			  sh->starttime / 27, sh->endtime / 27);
	    }
	    pthread_mutex_unlock(&sh->lock);

	    if(pk->flags & TCVP_PKT_FLAG_PTS){
		if(pk->pts > sh->endtime){
		    tc2_print("STREAM", TC2_PRINT_DEBUG,
			      "stream %i end time reached\n", pk->stream);
		    tcfree(pk);
		    pk = NULL;
		    sp->ms->used_streams[ps] = 0;
		} else if(str->starttime == -1LL){
		    tc2_print("STREAM", TC2_PRINT_DEBUG,
			      "stream %i start %llu\n",
			      pk->stream, pk->pts / 27);
		    sp->ms->streams[ps].common.start_time = pk->pts;
		    str->starttime = pk->pts;
		}
	    }

	    switch(str->probe){
	    case PROBE_AGAIN:
		tc2_print("STREAM", TC2_PRINT_DEBUG, "probing stream %i\n",
			  pk->stream);
		tcref(pk);
		str->probe = str->pipe->probe(str->pipe, pk,
					      sp->ms->streams + ps);
		if(str->probe == PROBE_FAIL){
		    tc2_print("STREAM", TC2_PRINT_DEBUG,
			      "stream %i failed probe\n", pk->stream);
		    del_stream(sp, ps);
		    tcfree(pk);
		    break;
		} else if(str->probe == PROBE_OK){
		    stream_time(sp->ms, ps, str->pipe);
		    tcvp_event_send(sh->sq, TCVP_LOAD, sp->ms);
		    pthread_create(&str->th, NULL, play_stream, str);
		    if(str->end->start)
			str->end->start(str->end);
		}
	    case PROBE_OK:
		pthread_mutex_lock(&sp->lock);
		if(str->packets){
		    tclist_push(str->packets, pk);
		    if(tclist_items(str->packets) > min_buffer)
			sp->nbuf &= ~(1 << ps);
		}
		pthread_cond_broadcast(&sp->cond);
		pthread_mutex_unlock(&sp->lock);
		break;
	    }
	    break;
	}
    }

    tc2_print("STREAM", TC2_PRINT_DEBUG, "read_stream done\n");

    return NULL;
}

static int
s_start(tcvp_pipe_t *tp)
{
    stream_player_t *sp = tp->private;
    int i;

    pthread_mutex_lock(&sp->lock);
    sp->state = RUN;
    pthread_cond_broadcast(&sp->cond);
    for(i = 0; i < sp->nstreams; i++){
	if(sp->streams[i].probe == PROBE_OK &&
	   sp->streams[i].end && sp->streams[i].end->start)
	    sp->streams[i].end->start(sp->streams[i].end);
    }
    pthread_mutex_unlock(&sp->lock);

    return 0;
}

static int
s_stop(tcvp_pipe_t *tp)
{
    stream_player_t *sp = tp->private;
    int i;

    pthread_mutex_lock(&sp->lock);
    sp->state = PAUSE;
    while(sp->waiting < sp->pstreams)
	pthread_cond_wait(&sp->cond, &sp->lock);
    for(i = 0; i < sp->nstreams; i++){
	if(sp->streams[i].probe == PROBE_OK &&
	   sp->streams[i].end && sp->streams[i].end->stop)
	    sp->streams[i].end->stop(sp->streams[i].end);
    }
    pthread_mutex_unlock(&sp->lock);

    return 0;
}

static int
s_flush(tcvp_pipe_t *tp, int drop)
{
    stream_player_t *sp = tp->private;
    packet_t *pk;
    int i;

    tc2_print("STREAM", TC2_PRINT_DEBUG, "flushing, drop=%i\n", drop);

    for(i = 0; i < sp->nstreams; i++){
	while((pk = tclist_shift(sp->streams[i].packets)))
	    tcfree(pk);
	if(sp->streams[i].pipe)
	    sp->streams[i].pipe->flush(sp->streams[i].pipe, drop);
	if(sp->streams[i].probe == PROBE_OK)
	    sp->nbuf |= 1 << i;
    }

    tc2_print("STREAM", TC2_PRINT_DEBUG, "flush complete\n", drop);
    return 0;
}

static void
s_free(void *p)
{
    tcvp_pipe_t *tp = p;
    stream_player_t *sp = tp->private;
    int i;

    pthread_mutex_lock(&sp->lock);
    sp->state = STOP;
    pthread_cond_broadcast(&sp->cond);
    pthread_mutex_unlock(&sp->lock);

    pthread_join(sp->rth, NULL);

    for(i = 0; i < sp->nstreams; i++){
	if(sp->streams[i].th)
	    pthread_join(sp->streams[i].th, NULL);
    }

    tcfree(sp->ms);
    free(sp->streams);
    free(sp);
}

extern tcvp_pipe_t *
s_play(stream_shared_t *sh, muxed_stream_t *ms)
{
    stream_player_t *sp;
    tcvp_pipe_t *p;
    int i;

    sp = calloc(1, sizeof(*sp));
    sp->ms = tcref(ms);
    sp->state = PAUSE;
    sp->shared = sh;
    pthread_mutex_init(&sp->lock, NULL);
    pthread_cond_init(&sp->cond, NULL);

    for(i = 0; i < ms->n_streams; i++)
	add_stream(sp, i);

    if(!sp->pstreams)
	return NULL;		/* FIXME: leak */

    pthread_mutex_lock(&sh->lock);
    sh->nstreams++;
    pthread_mutex_unlock(&sh->lock);

    pthread_create(&sp->rth, NULL, read_stream, sp);

    p = tcallocdz(sizeof(*p), NULL, s_free);
    p->private = sp;
    p->start = s_start;
    p->stop = s_stop;
    p->flush = s_flush;

    return p;
}

static void
free_shared(void *p)
{
    stream_shared_t *sh = p;

    tcfree(sh->profile);
    tcfree(sh->conf);
    tcfree(sh->timer);
    tchash_destroy(sh->filters, tcfree);
    if(sh->outfile)
	free(sh->outfile);
}

extern stream_shared_t *
new_shared(tcconf_section_t *profile, tcconf_section_t *conf,
	   tcvp_timer_t *timer, char *out)
{
    stream_shared_t *sh;
    char *qname, *qn;
    int pt;

    sh = tcallocdz(sizeof(*sh), NULL, free_shared);
    sh->profile = tcref(profile);
    sh->conf = tcref(conf);
    sh->timer = tcref(timer);
    sh->filters = tchash_new(16, TC_LOCK_SLOPPY, 0);
    pthread_mutex_init(&sh->lock, NULL);
    pthread_cond_init(&sh->cond, NULL);
    sh->vs = -1;
    sh->as = -1;
    sh->starttime = -1LL;
    sh->endtime = -1LL;
    sh->playtime = -1LL;
    if(out)
	sh->outfile = strdup(out);

    if(tcconf_getvalue(conf, "play_time", "%i", &pt) > 0)
	sh->playtime = pt * 27000000LL;

    tcconf_getvalue(conf, "qname", "%s", &qname);
    qn = alloca(strlen(qname) + 9);
    sprintf(qn, "%s/status", qname);
    free(qname);
    sh->sq = eventq_new(NULL);
    eventq_attach(sh->sq, qn, EVENTQ_SEND);

    return sh;
}
