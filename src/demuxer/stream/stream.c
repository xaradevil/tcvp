/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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
#include <pthread.h>
#include <semaphore.h>
#include <tcalloc.h>
#include <tclist.h>
#include <tcvp_types.h>
#include <stream_tc2.h>

#ifdef HAVE_LIBMAGIC
#include <magic.h>

static magic_t file_magic;
#endif

#define magic_size tcvp_demux_stream_conf_magic_size

static int TCVP_STATE;

static char *suffix_map[][2] = {
    { ".ogg", "audio/x-ogg" },
    { ".avi", "video/x-avi" },
    { ".mp3", "audio/mpeg" },
    { ".wav", "audio/x-wav" },
    { ".mov", "video/quicktime" },
    { ".mpg", "video/mpeg" },
    { ".mpeg", "video/mpeg" },
    { NULL, NULL }
};

extern muxed_stream_t *
s_open(char *name, tcconf_section_t *cs, tcvp_timer_t *t)
{
    const char *mg;
    char *m = NULL;
    url_t *u;
    char *buf[magic_size];
    int mgs;
    demux_open_t sopen;
    muxed_stream_t *ms;

    if(!(u = url_open(name, "r")))
	return NULL;

#ifdef HAVE_LIBMAGIC
    mgs = u->read(buf, 1, magic_size, u);
    u->seek(u, 0, SEEK_SET);
    if(mgs < magic_size)
	return NULL;
    mg = magic_buffer(file_magic, buf, mgs);
    if(mg){
	int e;
	m = strdup(mg);
	e = strcspn(m, " ;");
	m[e] = 0;
	if(strncmp(m, "audio/", 6) && strncmp(m, "video/", 6)){
	    free(m);
	    m = NULL;
	}
    }
#endif

    if(!m){
	char *s = strrchr(name, '.');
	if(s){
	    int i;
	    for(i = 0; suffix_map[i][0]; i++){
		if(!strcmp(s, suffix_map[i][0])){
		    m = strdup(suffix_map[i][1]);
		    break;
		}
	    }
	}
    }

    if(!m){
	m = strdup("video/mpeg");
    }

    if(!(sopen = tc2_get_symbol(m, "open")))
	return NULL;
    
    free(m);

    ms = sopen(name, u, cs, t);
    if(ms)
	tcattr_set(ms, "file", strdup(name), NULL, free);
    else
	u->close(u);
    return ms;
}

extern packet_t *
s_next_packet(muxed_stream_t *ms, int stream)
{
    return ms->next_packet(ms, stream);
}

extern int
s_validate(char *name, tcconf_section_t *cs)
{
    muxed_stream_t *ms = s_open(name, cs, NULL);
    packet_t *pk;
    int i;

    if(!ms)
	return -1;

    for(i = 0; i < ms->n_streams; i++)
	ms->used_streams[i] = 1;

    while((pk = ms->next_packet(ms, -1)))
	tcfree(pk);

    tcfree(ms);
    return 0;
}

#define RUN     1
#define PAUSE   2
#define STOP    3

#define min_buffer tcvp_demux_stream_conf_buffer

typedef struct s_play {
    muxed_stream_t *stream;
    tcvp_pipe_t **pipes;
    int nstreams;
    pthread_t *threads, rth;
    sem_t rsm;
    int state;
    int flushing;
    int waiting;
    pthread_mutex_t mtx;
    pthread_cond_t cnd;
    eventq_t sq;
    struct sp_stream {
	list *pq;
	sem_t ps;
	uint64_t pts;
	int eof;
    } *streams;
} s_play_t;

typedef struct vp_thread {
    int stream;
    s_play_t *vp;
} vp_thread_t;

static void
freeq(s_play_t *vp, int i)
{
    packet_t *pk;

    while((pk = list_shift(vp->streams[i].pq))){
	tcfree(pk);
    }
}

static int
wait_pause(s_play_t *vp)
{
    int w = 1;
    pthread_mutex_lock(&vp->mtx);
    while(vp->state == PAUSE){
	if(w){
	    vp->waiting++;
	    pthread_cond_broadcast(&vp->cnd);
	    w = 0;
	}
	pthread_cond_wait(&vp->cnd, &vp->mtx);
    }
    if(!w){
	vp->waiting--;
	pthread_cond_broadcast(&vp->cnd);
    }
    pthread_mutex_unlock(&vp->mtx);

    return vp->state != STOP;
}

static int
wstream(s_play_t *vp)
{
    int i, s = -1;
    uint64_t pts = -1;

    for(i = 0; i < vp->stream->n_streams; i++){
	if(vp->stream->used_streams[i] &&
	   !vp->streams[i].eof &&
	   vp->streams[i].pts < pts &&
	   list_items(vp->streams[i].pq) < min_buffer){
	    s = i;
	    pts = vp->streams[i].pts;
	}
    }

    return s;
}

static void *
read_stream(void *p)
{
    s_play_t *vp = p;
    int s;

    for(;;){
	sem_wait(&vp->rsm);
	if(vp->state == STOP)
	    break;

	while((s = wstream(vp)) >= 0 && vp->state == RUN){
	    packet_t *p = vp->stream->next_packet(vp->stream, s);
	    int str;

	    if(!p){
		vp->streams[s].eof = 1;
		sem_post(&vp->streams[s].ps);
		break;
	    }

	    str = p->stream;
	    if(p->flags & TCVP_PKT_FLAG_PTS)
		vp->streams[str].pts = p->pts;
	    list_push(vp->streams[str].pq, p);
	    sem_post(&vp->streams[str].ps);
	}
    }

    return NULL;
}

static packet_t *
get_packet(s_play_t *vp, int s)
{
    packet_t *p;

    if(!vp->streams[s].eof && sem_trywait(&vp->streams[s].ps)){
	sem_post(&vp->rsm);
	sem_wait(&vp->streams[s].ps);
    }
    p = list_shift(vp->streams[s].pq);
    if(list_items(vp->streams[s].pq) < min_buffer && !vp->streams[s].eof)
	sem_post(&vp->rsm);
    return p;
}

static void *
play_stream(void *p)
{
    vp_thread_t *vt = p;
    s_play_t *vp = vt->vp;
    int str = vt->stream;
    packet_t *pk;

    while(wait_pause(vp)){
	pk = get_packet(vp, str);
	if(!pk)
	    break;
	vp->pipes[str]->input(vp->pipes[str], pk);
    }

    pk = tcalloc(sizeof(*pk));
    pk->stream = str;
    pk->data = NULL;
    vp->pipes[str]->input(vp->pipes[str], pk);

    if(vp->state != STOP)
	vp->pipes[str]->flush(vp->pipes[str], 0);

    pthread_mutex_lock(&vp->mtx);
    vp->nstreams--;
    if(vp->nstreams == 0)
	tcvp_event_send(vp->sq, TCVP_STATE, TCVP_STATE_END);
    pthread_cond_broadcast(&vp->cnd);
    pthread_mutex_unlock(&vp->mtx);

    free(vt);
    return NULL;
}

static int
start(tcvp_pipe_t *p)
{
    s_play_t *vp = p->private;

    pthread_mutex_lock(&vp->mtx);
    vp->state = RUN;
    pthread_cond_broadcast(&vp->cnd);
    pthread_mutex_unlock(&vp->mtx);

    return 0;
}

static int
stop(tcvp_pipe_t *p)
{
    s_play_t *vp = p->private;

    vp->state = PAUSE;
    pthread_mutex_lock(&vp->mtx);
    while(vp->waiting < vp->nstreams)
	pthread_cond_wait(&vp->cnd, &vp->mtx);
    pthread_mutex_unlock(&vp->mtx);

    return 0;
}

static int
s_flush(tcvp_pipe_t *p, int drop)
{
    s_play_t *vp = p->private;
    int i;

    pthread_mutex_lock(&vp->mtx);
    vp->flushing++;
    pthread_mutex_unlock(&vp->mtx);

    while(!sem_trywait(&vp->rsm));

    for(i = 0; i < vp->stream->n_streams; i++){
	if(vp->stream->used_streams[i]){
	    vp->pipes[i]->flush(vp->pipes[i], drop);
	    if(drop){
		freeq(vp, i);
		while(!sem_trywait(&vp->streams[i].ps));
	    }
	}
	vp->streams[i].eof = 0;
    }

    pthread_mutex_lock(&vp->mtx);
    if(--vp->flushing == 0)
	pthread_cond_broadcast(&vp->cnd);
    pthread_mutex_unlock(&vp->mtx);

    return 0;
}

static void
s_free(void *p)
{
    tcvp_pipe_t *tp = p;
    s_play_t *vp = tp->private;
    int i, j;

    vp->state = STOP;
    s_flush(tp, 1);
    sem_post(&vp->rsm);
    pthread_join(vp->rth, NULL);
    pthread_mutex_lock(&vp->mtx);
    pthread_cond_broadcast(&vp->cnd);
    while(vp->nstreams > 0)
	pthread_cond_wait(&vp->cnd, &vp->mtx);
    pthread_mutex_unlock(&vp->mtx);

    for(i = 0, j = 0; i < vp->stream->n_streams; i++){
	if(vp->stream->used_streams[i]){
	    pthread_join(vp->threads[j], NULL);
	    freeq(vp, i);
	    list_destroy(vp->streams[i].pq, NULL);
	    j++;
	}
    }

    pthread_mutex_lock(&vp->mtx);
    while(vp->flushing)
	pthread_cond_wait(&vp->cnd, &vp->mtx);
    pthread_mutex_unlock(&vp->mtx);

    eventq_delete(vp->sq);
    free(vp->pipes);
    free(vp->threads);
    free(vp->streams);
    free(vp);
}

static int
s_probe(s_play_t *vp, tcvp_pipe_t **codecs)
{
    muxed_stream_t *ms = vp->stream;
    int pcnt[ms->n_streams];
    int pstat[ms->n_streams];
    int stime[ms->n_streams];
    int spc = 0;
    int i;

    memset(pcnt, 0, ms->n_streams * sizeof(pcnt[0]));
    memset(pstat, 0, ms->n_streams * sizeof(pstat[0]));
    memset(stime, 0, ms->n_streams * sizeof(stime[0]));

    for(i = 0; i < ms->n_streams; i++)
	if(ms->used_streams[i])
	    spc++;

    while(spc){
	packet_t *pk = ms->next_packet(ms, -1);
	int st = pk->stream;

	if(!pk || !pk->data){
	    if(pk)
		tcfree(pk);
	    continue;
	}
	
	if(ms->used_streams[st] && codecs[st]->probe){
	    int ps;

	    if(!pstat[st] || pstat[st] == PROBE_AGAIN){
		tcref(pk);
		ps = codecs[st]->probe(codecs[st], pk, &ms->streams[st]);
		pcnt[st]++;
		pstat[st] = ps;
		if(ps != PROBE_AGAIN ||
		   pcnt[st] > tcvp_demux_stream_conf_max_probe){
		    spc--;
		}
		if(ps == PROBE_FAIL){
		    ms->used_streams[st] = 0;
		} else {
		    list_push(vp->streams[st].pq, pk);
		}
		if(!stime[st] && pk->flags & TCVP_PKT_FLAG_PTS){
		    ms->streams[st].common.start_time = pk->pts;
		    stime[st] = 1;
		}
	    } else if(pstat[st] == PROBE_OK){
		list_push(vp->streams[st].pq, pk);
	    }
	}
    }

    for(i = 0; i < ms->n_streams; i++){
	if(pstat[i] != PROBE_OK){
	    ms->used_streams[i] = 0;
	    freeq(vp, i);
	    list_destroy(vp->streams[i].pq, NULL);
	}
	if(codecs[i])
	    codecs[i]->flush(codecs[i], 1);
    }

    return PROBE_OK;
}

extern tcvp_pipe_t *
s_play(muxed_stream_t *ms, tcvp_pipe_t **out, tcconf_section_t *cs)
{
    tcvp_pipe_t *p;
    s_play_t *vp;
    int i, j;
    char *qname, *qn;

    vp = calloc(1, sizeof(*vp));
    vp->stream = ms;
    vp->pipes = calloc(ms->n_streams, sizeof(*vp->pipes));
    vp->threads = calloc(ms->n_streams, sizeof(*vp->threads));
    vp->streams = calloc(ms->n_streams, sizeof(*vp->streams));
    vp->state = RUN;
    pthread_mutex_init(&vp->mtx, NULL);
    pthread_cond_init(&vp->cnd, NULL);
    sem_init(&vp->rsm, 0, 0);

    tcconf_getvalue(cs, "qname", "%s", &qname);
    qn = alloca(strlen(qname) + 9);
    sprintf(qn, "%s/status", qname);
    free(qname);
    vp->sq = eventq_new(NULL);
    eventq_attach(vp->sq, qn, EVENTQ_SEND);

    for(i = 0; i < ms->n_streams; i++){
	vp->streams[i].pq = list_new(TC_LOCK_SLOPPY);
	sem_init(&vp->streams[i].ps, 0, 0);
    }

    s_probe(vp, out);
    pthread_create(&vp->rth, NULL, read_stream, vp);
    vp->state = PAUSE;

    for(i = 0, j = 0; i < ms->n_streams; i++){
	if(ms->used_streams[i]){
	    vp_thread_t *th = malloc(sizeof(*th));
	    th->stream = i;
	    th->vp = vp;
	    vp->pipes[i] = out[i];
	    pthread_create(&vp->threads[j], NULL, play_stream, th);
	    j++;
	}
    }

    vp->nstreams = j;

    p = tcallocdz(sizeof(tcvp_pipe_t), NULL, s_free);
    p->input = NULL;
    p->start = start;
    p->stop = stop;
    p->flush = s_flush;
    p->private = vp;

    return p;
}

extern int
s_init(char *p)
{
    TCVP_STATE = tcvp_event_get("TCVP_STATE");

#ifdef HAVE_LIBMAGIC
    file_magic = magic_open(MAGIC_MIME);
    magic_load(file_magic, NULL);
#endif

    return 0;
}

extern int
s_shdn(void)
{
#ifdef HAVE_LIBMAGIC
    magic_close(file_magic);
#endif
    return 0;
}
