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
#include <pthread.h>
#include <semaphore.h>
#include <tcalloc.h>
#include <tclist.h>
#include <tcvp_types.h>
#include <tcvp_event.h>
#include <stream_tc2.h>

extern muxed_stream_t *
s_open(char *name, conf_section *cs)
{
    char *m = NULL;
    char *ext;

    ext = strrchr(name, '.');

    if(ext++){
	if(!strcmp(ext, "ogg")) {
	    m = "audio/ogg";
	} else if(!strcmp(ext, "avi")){
	    m = "video/x-avi";
	} else if(!strcmp(ext, "mp3")){
	    m = "audio/mp3";
	} else if(!strcmp(ext, "wav")){
	    m = "audio/x-wav";
	}
    }

    if(!m){
	m = "video/mpeg";
    }

    stream_open_t sopen = tc2_get_symbol(m, "open");
    return sopen(name, cs);
}

extern packet_t *
s_next_packet(muxed_stream_t *ms, int stream)
{
    return ms->next_packet(ms, stream);
}

extern int
s_validate(char *name, conf_section *cs)
{
    muxed_stream_t *ms = s_open(name, cs);

    if(!ms)
	return -1;

    while(ms->next_packet(ms, -1));

    tcfree(ms);
    return 0;
}

#define RUN     1
#define PAUSE   2
#define STOP    3
#define PROBE   4

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
    } *streams;
    int eof;
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
	pk->free(pk);
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
	if(vp->streams[i].pts < pts &&
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

	while((s = wstream(vp)) >= 0 && vp->state != STOP){
	    packet_t *p = vp->stream->next_packet(vp->stream, s);
	    if(!p){
		pthread_mutex_lock(&vp->mtx);
		vp->eof = 1;
		pthread_cond_broadcast(&vp->cnd);
		pthread_mutex_unlock(&vp->mtx);
		for(s = 0; s < vp->stream->n_streams; s++)
		    sem_post(&vp->streams[s].ps);
		break;
	    }
	    if(p->flags & TCVP_PKT_FLAG_PTS)
		vp->streams[p->stream].pts = p->pts;
	    list_push(vp->streams[p->stream].pq, p);
	    sem_post(&vp->streams[p->stream].ps);
	}
    }

    return NULL;
}

static packet_t *
get_packet(s_play_t *vp, int s)
{
    packet_t *p;

    if(sem_trywait(&vp->streams[s].ps)){
	sem_post(&vp->rsm);
	sem_wait(&vp->streams[s].ps);
    }
    p = list_shift(vp->streams[s].pq);
    if(list_items(vp->streams[s].pq) < min_buffer && !vp->eof)
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

    pk = malloc(sizeof(*pk));
    pk->stream = str;
    pk->data = NULL;
    pk->free = (typeof(pk->free)) free;
    vp->pipes[str]->input(vp->pipes[str], pk);

    if(vp->state != STOP)
	vp->pipes[str]->flush(vp->pipes[str], 0);

    pthread_mutex_lock(&vp->mtx);
    vp->nstreams--;
    if(vp->nstreams == 0){
	tcvp_state_event_t *te = tcvp_alloc_event(TCVP_STATE, TCVP_STATE_END);
	eventq_send(vp->sq, te);
	tcfree(te);
    }
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

    pthread_mutex_lock(&vp->mtx);
    vp->state = PAUSE;
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

    for(i = 0; i < vp->stream->n_streams; i++){
	if(vp->stream->used_streams[i]){
	    vp->pipes[i]->flush(vp->pipes[i], drop);
	    if(drop){
		freeq(vp, i);
		while(!sem_trywait(&vp->streams[i].ps));
	    }
	}
    }

    vp->eof = 0;

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

    stop(tp);
    vp->state = STOP;
    sem_post(&vp->rsm);
    s_flush(tp, 1);
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
    int i;

    for(i = 0; i < ms->n_streams; i++){
	if(ms->used_streams[i] && codecs[i]->probe){
	    int p = PROBE_FAIL;
	    int st = 0;
	    do {
		packet_t *pk = get_packet(vp, i);
		if(!pk->data){
		    pk->free(pk);
		    break;
		}
		if(!st && pk->flags & TCVP_PKT_FLAG_PTS)
		    ms->streams[i].common.start_time = pk->pts;
		p = codecs[i]->probe(codecs[i], pk, &ms->streams[i]);
	    } while(p == PROBE_AGAIN);
	    if(p != PROBE_OK){
		ms->used_streams[i] = 0;
	    }
	}
    }

    return PROBE_OK;
}

extern tcvp_pipe_t *
s_play(muxed_stream_t *ms, tcvp_pipe_t **out, conf_section *cs)
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
    vp->state = PROBE;
    pthread_mutex_init(&vp->mtx, NULL);
    pthread_cond_init(&vp->cnd, NULL);
    sem_init(&vp->rsm, 0, 0);

    conf_getvalue(cs, "qname", "%s", &qname);
    qn = alloca(strlen(qname) + 9);
    sprintf(qn, "%s/status", qname);
    vp->sq = eventq_new(NULL);
    eventq_attach(vp->sq, qn, EVENTQ_SEND);

    for(i = 0; i < ms->n_streams; i++){
	vp->streams[i].pq = list_new(TC_LOCK_SLOPPY);
	sem_init(&vp->streams[i].ps, 0, 0);
    }

    pthread_create(&vp->rth, NULL, read_stream, vp);
    s_probe(vp, out);
    vp->state = PAUSE;

    for(i = 0, j = 0; i < ms->n_streams; i++){
	if(ms->used_streams[i]){
	    vp_thread_t *th = malloc(sizeof(*th));
	    th->stream = i;
	    th->vp = vp;
	    vp->pipes[i] = out[i];
	    pthread_create(&vp->threads[j], NULL, play_stream, th);
	    j++;
	} else {
	    list_destroy(vp->streams[i].pq, NULL);
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
