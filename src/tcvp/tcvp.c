/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tchash.h>
#include <tcvp_types.h>
#include <tcvp_event.h>
#include <tcvp_core_tc2.h>

typedef struct tcvp_player {
    tcvp_pipe_t *demux, *audio, *video, *aend, *vend;
    muxed_stream_t *stream;
    timer__t *timer;
    pthread_t th_ticker, th_event;
    pthread_mutex_t tmx;
    pthread_cond_t tcd;
    int state;
    eventq_t qs, qr, qt;
    conf_section *conf;
    int open;
    hash_table *filters;
} tcvp_player_t;

static int
t_start(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    if(tp->demux)
	tp->demux->start(tp->demux);

    if(tp->vend && tp->vend->start)
	tp->vend->start(tp->vend);

    if(tp->aend && tp->aend->start)
	tp->aend->start(tp->aend);

    if(tp->timer)
	tp->timer->start(tp->timer);

    tp->state = TCVP_STATE_PLAYING;
    tcvp_state_event_t *te = tcvp_alloc_event(TCVP_STATE, TCVP_STATE_PLAYING);
    eventq_send(tp->qs, te);
    tcfree(te);

    return 0;
}

static int
t_stop(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    if(tp->demux)
	tp->demux->stop(tp->demux);

    if(tp->timer)
	tp->timer->stop(tp->timer);

    if(tp->aend && tp->aend->stop)
	tp->aend->stop(tp->aend);

    if(tp->vend && tp->vend->stop)
	tp->vend->stop(tp->vend);

    tp->state = TCVP_STATE_STOPPED;
    tcvp_state_event_t *te = tcvp_alloc_event(TCVP_STATE, TCVP_STATE_STOPPED);
    eventq_send(tp->qs, te);
    tcfree(te);

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

    pthread_mutex_lock(&tp->tmx);

    if(tp->demux){
	tcfree(tp->demux);
	tp->demux = NULL;
    }

    if(tp->audio){
	close_pipe(tp->audio);
	tp->audio = NULL;
	tp->aend = NULL;
    }

    if(tp->video){
	close_pipe(tp->video);
	tp->video = NULL;
	tp->vend = NULL;
    }

    if(tp->stream){
	tcfree(tp->stream);
	tp->stream = NULL;
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
	tp->timer->free(tp->timer);
	tp->timer = NULL;
    }

    tp->open = 0;
    pthread_mutex_unlock(&tp->tmx);

    return 0;
}

static void
t_free(player_t *pl)
{
    tcvp_player_t *tp = pl->private;
    tcvp_event_t *te;

    te = tcvp_alloc_event(-1);
    eventq_send(tp->qr, te);
    tcfree(te);
    pthread_join(tp->th_event, NULL);

    t_close(pl);

    eventq_delete(tp->qr);
    eventq_delete(tp->qs);
    eventq_delete(tp->qt);
    pthread_mutex_destroy(&tp->tmx);
    pthread_cond_destroy(&tp->tcd);

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
print_info(muxed_stream_t *stream, tcvp_pipe_t **pipes)
{
    int i;

    for(i = 0; i < stream->n_streams; i++){
	int u = stream->used_streams[i];
	printf("%2i%*s: ", i, 1 - u, u? "*": "");
	print_stream(stream->streams + i);
	if(u){
	    tcvp_pipe_t *np = pipes[i];
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
	tcvp_timer_event_t *te = tcvp_alloc_event(TCVP_TIMER, time);
	eventq_send(tp->qt, te);
	tcfree(te);
	tp->timer->wait(tp->timer, time + 27000000, &tp->tmx);
    }
    pthread_mutex_unlock(&tp->tmx);

    return NULL;
}

static int
t_seek(player_t *pl, int64_t time, int how)
{
    tcvp_player_t *tp = pl->private;
    uint64_t ntime;

    if(tp->stream && tp->stream->seek){
	int s = tp->state;
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
	ntime = tp->stream->seek(tp->stream, ntime);
	if(ntime != -1LL){
	    pthread_mutex_lock(&tp->tmx);
	    tp->timer->reset(tp->timer, ntime);
	    tp->timer->interrupt(tp->timer);
	    pthread_mutex_unlock(&tp->tmx);
	    tp->demux->flush(tp->demux, 1);
	}
	if(s == TCVP_STATE_PLAYING){
	    t_start(pl);
	}
    }

    return 0;    
}

static tcvp_pipe_t *
new_pipe(tcvp_player_t *tp, stream_t *s, conf_section *p)
{
    tcvp_pipe_t *pipe = NULL, *pp = NULL, *pn = NULL;
    conf_section *f;
    void *cs = NULL;

    while((f = conf_nextsection(p, "filter", &cs))){
	char *type, *id = NULL;
	filter_new_t fn;

	pn = NULL;

	if(conf_getvalue(f, "type", "%s", &type) < 1)
	    continue;

	if(conf_getvalue(f, "id", "%s", &id) > 0)
	    hash_find(tp->filters, id, &pn);

	if(!pn){
	    if(!(fn = tc2_get_symbol(type, "new")))
		break;

	    if(!(pn = fn(s, tp->conf, &tp->timer)))
		break;

	    if(id)
		hash_replace(tp->filters, id, pn);
	}

	if(id)
	    tcref(pn);

	if(!pipe)
	    pipe = pn;

	if(pp)
	    pp->next = pn;
	pp = pn;
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
t_open(player_t *pl, char *name)
{
    int i;
    stream_t *as = NULL, *vs = NULL;
    tcvp_pipe_t **codecs;
    tcvp_pipe_t *demux = NULL;
    tcvp_pipe_t *video = NULL, *audio = NULL;
    muxed_stream_t *stream = NULL;
    tcvp_player_t *tp = pl->private;
    tcvp_load_event_t *te;
    int ac = -1, vc = -1;
    int start;
    char *profile = tcvp_conf_default_profile, prname[256];
    conf_section *prsec;

    if(!(stream = stream_open(name, tp->conf))){
	return -1;
    }

    codecs = calloc(stream->n_streams, sizeof(*codecs));

    if(tp->conf){
	if(conf_getvalue(tp->conf, "video/stream", "%i", &vc) > 0){
	    if(vc >= 0 && vc < stream->n_streams &&
	       stream->streams[vc].stream_type == STREAM_TYPE_VIDEO){
		vs = &stream->streams[vc];
	    } else {
		vc = -2;
	    }
	}
	if(conf_getvalue(tp->conf, "audio/stream", "%i", &ac) > 0){
	    if(ac >= 0 && ac < stream->n_streams &&
	       stream->streams[ac].stream_type == STREAM_TYPE_AUDIO){
		as = &stream->streams[ac];
	    } else {
		ac = -2;
	    }
	}
	conf_getvalue(tp->conf, "profile", "%s", &profile);
    }

    snprintf(prname, 256, "TCVP/profiles/%s", profile);
    if(!(prsec = tc2_get_conf(prname))){
	fprintf(stderr, "TCVP: No profile '%s'\n", profile);
	return -1;
    }

    tp->filters = hash_new(10, 0);

    for(i = 0; i < stream->n_streams; i++){
	stream_t *st = &stream->streams[i];
	conf_section *pc;

	if(stream->streams[i].stream_type == STREAM_TYPE_VIDEO &&
	   (!vs || i == vc) && vc > -2){
	    if((pc = conf_getsection(prsec, "video")) &&
	       (video = new_pipe(tp, st, pc))){
		vs = st;
		codecs[i] = video;
		stream->used_streams[i] = 1;
		vc = i;
	    } else if(vs){
		printf("Warning: Stream %i not supported => no video\n", i);
		vs = NULL;
	    }
	} else if(stream->streams[i].stream_type == STREAM_TYPE_AUDIO &&
		  (!as || i == ac) && ac > -2){
	    if((pc = conf_getsection(prsec, "audio")) &&
	       (audio = new_pipe(tp, st, pc))){
		as = &stream->streams[i];
		codecs[i] = audio;
		stream->used_streams[i] = 1;
		ac = i;
	    } else if(as){
		printf("Warning: Stream %i not supported => no audio\n", i);
		as = NULL;
	    }
	}
    }

    hash_destroy(tp->filters, tcfree);

    if(!as && !vs)
	goto err;

    tp->open = 1;

    if(!tp->timer){
	tp->timer = timer_new(270000);
    }

    demux = stream_play(stream, codecs, tp->conf);
    print_info(stream, codecs);

    if(!stream->time){
	for(i = 0; i < stream->n_streams; i++){
	    if(stream->used_streams[i]){
		tcvp_pipe_t *p = codecs[i];
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
			    stream->streams[i].audio.sample_rate = 
				st->audio.sample_rate;
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

    tp->state = TCVP_STATE_STOPPED;
    if(vc >= 0 && stream->used_streams[vc]){
	tp->video = video;
	tp->vend = pipe_end(video);
    } else {
	close_pipe(video);
	video = NULL;
    }
    if(ac >= 0 && stream->used_streams[ac]){
	tp->audio = audio;
	tp->aend = pipe_end(audio);
    } else {
	close_pipe(audio);
	audio = NULL;
    }
    if(!audio && !video)
	goto err;
    tp->demux = demux;
    tp->stream = stream;

    if(conf_getvalue(tp->conf, "start_time", "%i", &start) == 1){
	uint64_t spts = (uint64_t) start * 27000000LL;
	t_seek(pl, spts, TCVP_SEEK_ABS);
    }

    pthread_create(&tp->th_ticker, NULL, st_ticker, tp);

    demux->start(demux);
    if(tp->vend && tp->vend->buffer)
	tp->vend->buffer(tp->vend, 0.9);
    if(tp->aend && tp->aend->buffer)
	tp->aend->buffer(tp->aend, 0.9);

    free(codecs);

    te = tcvp_alloc_event(TCVP_LOAD, stream);
    eventq_send(tp->qs, te);
    tcfree(te);

    return 0;

err:
    printf("No supported streams found.\n");
    if(demux)
	tcfree(demux);
    tcfree(stream);
    return -1;
}

static void *
t_event(void *p)
{
    player_t *pl = p;
    tcvp_player_t *tp = pl->private;
    int r = 1;

    while(r){
	tcvp_event_t *te = eventq_recv(tp->qr);
	switch(te->type){
	case TCVP_OPEN:
	    if(t_open(pl, te->open.file) < 0){
		tcvp_state_event_t *te = tcvp_alloc_event(TCVP_STATE,
							  TCVP_STATE_ERROR);
		eventq_send(tp->qs, te);
		tcfree(te);
	    }		
	    break;

	case TCVP_START:
	    t_start(pl);
	    break;

	case TCVP_STOP:
	    t_stop(pl);
	    break;

	case TCVP_PAUSE:
	    if(tp->state == TCVP_STATE_PLAYING)
		t_stop(pl);
	    else if(tp->state == TCVP_STATE_STOPPED)
		t_start(pl);
	    break;

	case TCVP_SEEK:
	    t_seek(pl, te->seek.time, te->seek.how);
	    break;

	case TCVP_CLOSE:
	    t_close(pl);
	    break;

	case -1:
	    r = 0;
	    break;
	}
	tcfree(te);
    }
    return NULL;
}

static int
q_cmd(player_t *pl, int cmd)
{
    tcvp_player_t *tp = pl->private;
    tcvp_event_t *te = tcvp_alloc_event(cmd);
    eventq_send(tp->qr, te);
    tcfree(te);
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
    tcvp_seek_event_t *se = tcvp_alloc_event(TCVP_SEEK, pts, TCVP_SEEK_ABS);
    eventq_send(tp->qr, se);
    tcfree(se);
    return 0;
}

static u_int plc;

extern player_t *
t_new(conf_section *cs)
{
    tcvp_player_t *tp;
    player_t *pl;
    char qn[32];
    char qname[16];

    tp = calloc(1, sizeof(*tp));
    pl = malloc(sizeof(*pl));
    pl->start = q_start;
    pl->stop = q_stop;
    pl->seek = q_seek;
    pl->close = q_close;
    pl->free = t_free;
    pl->private = tp;

    sprintf(qname, "TCVP-%u", plc++);
    conf_setvalue(cs, "qname", "%s", qname);

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
    tp->conf = cs;

    return pl;
}
