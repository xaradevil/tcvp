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

#define QSIZE tcvp_demux_stream_conf_buffer

typedef struct packetq {
    packet_t **q;
    int head, tail, count;
} packetq_t;

typedef struct s_play {
    muxed_stream_t *stream;
    tcvp_pipe_t **pipes;
    int streams;
    pthread_t *threads, rth;
    int state;
    int flushing;
    int waiting;
    pthread_mutex_t mtx;
    pthread_cond_t cnd;
    eventq_t sq;
    packetq_t *pq;
    int eof;
} s_play_t;

typedef struct vp_thread {
    int stream;
    s_play_t *vp;
} vp_thread_t;

static void
qpk(s_play_t *vp, packet_t *pk, int s)
{
    packetq_t *pq = vp->pq + s;

    pthread_mutex_lock(&vp->mtx);

    while(pq->count == QSIZE && vp->state != STOP)
	pthread_cond_wait(&vp->cnd, &vp->mtx);

    if(vp->state != STOP){
	pq->q[pq->head] = pk;
	if(++pq->head == QSIZE)
	    pq->head = 0;
	pq->count++;
	pthread_cond_broadcast(&vp->cnd);
    }

    pthread_mutex_unlock(&vp->mtx);
}

static packet_t *
dqp(s_play_t *vp, int s)
{
    packet_t *pk = NULL;
    packetq_t *pq = vp->pq + s;

    pthread_mutex_lock(&vp->mtx);
    while(!pq->count)
	pthread_cond_wait(&vp->cnd, &vp->mtx);

    pk = pq->q[pq->tail];
    if(++pq->tail == QSIZE)
	pq->tail = 0;
    pq->count--;
    pthread_cond_broadcast(&vp->cnd);

    pthread_mutex_unlock(&vp->mtx);

    return pk;
}

static void
freeq(s_play_t *vp, int i)
{
    while(vp->pq[i].count){
	packet_t *pk = dqp(vp, i);
	if(pk)
	    pk->free(pk);
    }
}

static void
wait_pause(s_play_t *vp, int n, int eof)
{
    int w = 1;
    pthread_mutex_lock(&vp->mtx);
    while((vp->state == PAUSE || (vp->eof && eof)) &&
	  vp->streams > 0
	  && vp->waiting >= n){
	if(w){
	    vp->waiting++;
	    pthread_cond_broadcast(&vp->cnd);
	    w = 0;
	    n = 0;
	}
	pthread_cond_wait(&vp->cnd, &vp->mtx);
    }
    if(!w){
	vp->waiting--;
	pthread_cond_broadcast(&vp->cnd);
    }
    pthread_mutex_unlock(&vp->mtx);
}

static void *
read_stream(void *p)
{
    s_play_t *vp = p;
    muxed_stream_t *ms = vp->stream;
    int i;

    while(vp->state != STOP){
	packet_t *pk;
	int s;

	wait_pause(vp, 0, 1);

	for(i = 0, s = 0; i < ms->n_streams; i++){
	    if(ms->used_streams[i] && vp->pq[i].count < vp->pq[s].count)
		s = i;
	}

	if(!(pk = ms->next_packet(ms, s))){
	    for(i = 0; i < ms->n_streams; i++)
		if(ms->used_streams[i])
		    qpk(vp, NULL, i);
	    vp->eof = 1;
	    continue;
	}

	qpk(vp, pk, pk->stream);
    }

    return NULL;
}

static void *
play_stream(void *p)
{
    vp_thread_t *vt = p;
    s_play_t *vp = vt->vp;
    int str = vt->stream;
    packet_t *pk;

    while(vp->state != STOP){
	wait_pause(vp, 1, 0);
	pk = dqp(vp, str);
	vp->pipes[str]->input(vp->pipes[str], pk);
	if(!pk)
	    break;
    }

    if(vp->state != STOP)
	vp->pipes[str]->flush(vp->pipes[str], 0);

    pthread_mutex_lock(&vp->mtx);
    vp->streams--;
    if(vp->streams == 0){
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
    while(vp->waiting < vp->streams + 1)
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

static int
s_free(tcvp_pipe_t *p)
{
    s_play_t *vp = p->private;
    int i, j;

    pthread_mutex_lock(&vp->mtx);
    vp->state = STOP;
    pthread_cond_broadcast(&vp->cnd);
    while(vp->streams > 0)
	pthread_cond_wait(&vp->cnd, &vp->mtx);
    pthread_mutex_unlock(&vp->mtx);

    pthread_join(vp->rth, NULL);
    for(i = 0, j = 0; i < vp->stream->n_streams; i++){
	if(vp->stream->used_streams[i]){
	    pthread_join(vp->threads[j], NULL);
	    freeq(vp, i);
	    free(vp->pq[i].q);
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
    free(vp->pq);
    free(vp);
    free(p);

    return 0;
}

static int
s_probe(s_play_t *vp, tcvp_pipe_t **codecs)
{
    muxed_stream_t *ms = vp->stream;
    int i;

    for(i = 0; i < ms->n_streams; i++){
	if(ms->used_streams[i] && codecs[i]->probe){
	    int p = PROBE_FAIL;
	    do {
		packet_t *pk = dqp(vp, i);
		if(!pk)
		    break;
		p = codecs[i]->probe(codecs[i], pk, &ms->streams[i]);
	    } while(p == PROBE_AGAIN);
	    if(p == PROBE_FAIL){
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
    vp->pq = calloc(ms->n_streams, sizeof(*vp->pq));
    vp->state = PROBE;
    pthread_mutex_init(&vp->mtx, NULL);
    pthread_cond_init(&vp->cnd, NULL);

    conf_getvalue(cs, "qname", "%s", &qname);
    qn = alloca(strlen(qname) + 9);
    sprintf(qn, "%s/status", qname);
    vp->sq = eventq_new(NULL);
    eventq_attach(vp->sq, qn, EVENTQ_SEND);

    for(i = 0; i < ms->n_streams; i++)
	vp->pq[i].q = malloc(QSIZE * sizeof(*vp->pq->q));

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
	    free(vp->pq[i].q);
	}
    }

    vp->streams = j;

    p = calloc(1, sizeof(tcvp_pipe_t));
    p->input = NULL;
    p->start = start;
    p->stop = stop;
    p->free = s_free;
    p->flush = s_flush;
    p->private = vp;

    return p;
}
