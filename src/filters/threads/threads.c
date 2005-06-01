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

#include <stdlib.h>
#include <pthread.h>
#include <tcalloc.h>
#include <threads_tc2.h>

typedef struct threads threads_t;

typedef struct thr_packet {
    tcvp_data_packet_t *pk;
    int seq;
} thr_packet_t;

typedef struct thread {
    tcvp_pipe_t *pipe, *end;
    thr_packet_t *pkq;
    int head, tail, nq;
    tcvp_data_packet_t *pk;
    int seq;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t thr;
    struct thread *next;
    tcvp_player_t *sh;
    threads_t *th;
    int run;
    int free;
} thread_t;

struct threads {
    thread_t *cur;
    thread_t *free;
    pthread_mutex_t lock, olock;
    pthread_cond_t cond, ocond;
    thread_t *threads;
    int nthreads;
    thr_packet_t *outq;
    int tail;
    int iseq, oseq;
    int qsize, osize;
    int pkc, npk;
    tcvp_pipe_t *pipe;
    int run;
    pthread_t oth;
};

static int
wait_packet(thread_t *t)
{
    pthread_mutex_lock(&t->lock);

    tc2_print("THREADS", TC2_PRINT_DEBUG+3, "[%i] waiting for packets\n",
	      t - t->th->threads);
    while(!t->nq && t->run)
	pthread_cond_wait(&t->cond, &t->lock);
    if(!t->run)
	goto end;

    t->pk = t->pkq[t->tail].pk;
    t->seq = t->pkq[t->tail].seq;
    t->pkq[t->tail].pk = NULL;
    if(++t->tail == t->th->qsize)
	t->tail = 0;
    t->nq--;

    pthread_cond_broadcast(&t->cond);
end:
    pthread_mutex_unlock(&t->lock);
    return t->run;
}

static void *
th_run(void *p)
{
    thread_t *t = p;
    threads_t *th = t->th;

    tc2_print("THREADS", TC2_PRINT_DEBUG, "[%i] starting\n", t - th->threads);

    while(wait_packet(t)){
	tc2_print("THREADS", TC2_PRINT_DEBUG+1, "[%i] processing packet %i\n",
		  t - th->threads, t->seq);
	t->pipe->input(t->pipe, (tcvp_packet_t *) t->pk);
    }

    tc2_print("THREADS", TC2_PRINT_DEBUG, "[%i] done\n", t - th->threads);
    return NULL;
}

static int
th_input(tcvp_pipe_t *p, tcvp_packet_t *tpk)
{
    tcvp_data_packet_t *pk = (tcvp_data_packet_t *) tpk;
    thread_t *t = p->private;
    threads_t *th = t->th;
    int oqp;

    pthread_mutex_lock(&th->olock);
    while(t->seq - th->oseq >= th->osize && t->run)
	pthread_cond_wait(&th->cond, &th->olock);
    if(!t->run)
	goto end;

    oqp = th->tail + t->seq - th->oseq;
    if(oqp >= th->osize)
	oqp -= th->osize;

    if(th->outq[oqp].pk)
	tc2_print("THREADS", TC2_PRINT_WARNING,
		  "[%i] BUG: outq[%i] used, oseq=%i seq=%i\n", t - th->threads,
		  oqp, th->oseq, t->seq);

    tc2_print("THREADS", TC2_PRINT_DEBUG+3, "[%i] enqueuing packet %i @ %i\n",
	      t - th->threads, t->seq, oqp);
    th->outq[oqp].pk = pk;
    th->outq[oqp].seq = t->seq;

    pthread_cond_broadcast(&th->ocond);
end:
    pthread_mutex_unlock(&th->olock);

    return 0;
}

static void *
thr_output(void *p)
{
    threads_t *th = p;

    pthread_mutex_lock(&th->olock);
    while(!th->outq[th->npk / 2].pk && th->run)
	pthread_cond_wait(&th->ocond, &th->olock);
    pthread_mutex_unlock(&th->olock);
    if(!th->run)
	return NULL;

    while(th->run){
	tcvp_data_packet_t *opk;
	int pkn;

	pthread_mutex_lock(&th->olock);
	while(!th->outq[th->tail].pk && th->run)
	    pthread_cond_wait(&th->ocond, &th->olock);
	if(!th->run){
	    pthread_mutex_unlock(&th->olock);
	    break;
	}

	opk = th->outq[th->tail].pk;
	pkn = th->outq[th->tail].seq;

	th->outq[th->tail].pk = NULL;
	if(++th->tail == th->osize)
	    th->tail = 0;
	th->oseq = pkn + 1;
	pthread_cond_broadcast(&th->ocond);
	pthread_mutex_unlock(&th->olock);

	tc2_print("THREADS", TC2_PRINT_DEBUG+2, "sending packet %i\n", pkn);
	th->pipe->next->input(th->pipe->next, (tcvp_packet_t *) opk);
    }

    return NULL;
}

static int
th_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    thread_t *t = p->private;

    t->th->pipe->format = *s;

    return PROBE_OK;
}

static int
th_flush(tcvp_pipe_t *p, int drop)
{
    return 0;
}

extern int
thr_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    threads_t *th = p->private;
    thread_t *t;
    int ws = 0;

    th->pkc++;

    if(!th->cur || (th->cur->nq && th->pkc > th->npk)){
	u_int i, mq = -1, tn = 0;
	pthread_mutex_lock(&th->lock);

	for(i = 0; i < th->nthreads; i++){
	    if(th->threads[i].nq < mq){
		mq = th->threads[i].nq;
		tn = i;
	    }
	}
	t = th->threads + tn;

	if(th->cur != t){
	    th->pkc = 0;
	    pk->flags |= TCVP_PKT_FLAG_DISCONT;
	    tc2_print("THREADS", TC2_PRINT_DEBUG, "switching to thread %i\n",
		      t - th->threads);
	}

	th->cur = t;

	pthread_mutex_unlock(&th->lock);
    } else {
	t = th->cur;
    }

    pthread_mutex_lock(&t->lock);
    while(t->nq == th->qsize)
	pthread_cond_wait(&t->cond, &t->lock);

    tc2_print("THREADS", TC2_PRINT_DEBUG+4,
	      "enqueuing packet %i for thread %i\n",
	      th->iseq, t - th->threads);
    t->pkq[t->head].pk = pk;
    t->pkq[t->head].seq = th->iseq++;
    if(++t->head == th->qsize)
	t->head = 0;
    t->nq++;
    if(!pk->data)
	ws = th->iseq;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);

    if(ws){
	pthread_mutex_lock(&th->lock);
	while(th->oseq < ws)
	    pthread_cond_wait(&th->cond, &th->lock);
	pthread_mutex_unlock(&th->lock);
    }

    return 0;
}

extern int
thr_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    threads_t *th = p->private;
    int i, ps = PROBE_OK;

    for(i = 0; i < th->nthreads; i++){
	thread_t *t = th->threads + i;
	ps = t->pipe->probe(t->pipe, tcref(pk), s);
	t->run = 1;
	pthread_create(&t->thr, NULL, th_run, t);
    }

    if(ps == PROBE_OK){
	th->run = 1;
	pthread_create(&th->oth, NULL, thr_output, th);
    }

    return ps;
}

extern int
thr_flush(tcvp_pipe_t *p, int drop)
{
    threads_t *th = p->private;
    int i;

    for(i = 0; i < th->nthreads; i++)
	th->threads[i].pipe->flush(th->threads[i].pipe, drop);

    return 0;
}

static void
thr_free(void *p)
{
    threads_t *th = p;
    int i;

    for(i = 0; i < th->nthreads; i++){
	thread_t *t = th->threads + i;
	tc2_print("THREADS", TC2_PRINT_DEBUG, "stopping thread %i\n", i);
	pthread_mutex_lock(&t->lock);
	t->run = 0;
	pthread_cond_broadcast(&t->cond);
	pthread_mutex_unlock(&t->lock);
	pthread_join(t->thr, NULL);
	player_close_pipe(t->pipe);
	tcfree(t->sh);
	pthread_mutex_destroy(&t->lock);
	pthread_cond_destroy(&t->cond);
	free(t->pkq);
    }

    if(th->oth){
	pthread_mutex_lock(&th->olock);
	th->run = 0;
	pthread_cond_broadcast(&th->ocond);
	pthread_mutex_unlock(&th->olock);
	pthread_join(th->oth, NULL);
    }

    free(th->outq);
    free(th->threads);
    pthread_mutex_destroy(&th->lock);
    pthread_mutex_destroy(&th->olock);
    pthread_cond_destroy(&th->cond);
    pthread_cond_destroy(&th->ocond);

    tc2_request(TC2_DEL_DEPENDENCY, 0, "stream");
}

extern int
thr_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	muxed_stream_t *ms)
{
    threads_t *th;
    int osize = 0;
    int i;

    th = tcallocdz(sizeof(*th), NULL, thr_free);
    th->nthreads = 2;
    th->npk = 12;
    tcconf_getvalue(cs, "threads", "%i", &th->nthreads);
    tcconf_getvalue(cs, "min_packets", "%i", &th->npk);
    th->qsize = th->nthreads * th->npk;
    tcconf_getvalue(cs, "inqueue", "%i", &th->qsize);
    th->osize = th->nthreads * th->qsize;
    tcconf_getvalue(cs, "buffer", "%i", osize);
    if(osize > th->osize)
	th->osize = osize;

    if(th->nthreads < 1){
	tc2_print("THREADS", TC2_PRINT_ERROR,
		  "1 or more threads required, %i specified\n", th->nthreads);
	return -1;
    }

    pthread_mutex_init(&th->lock, NULL);
    pthread_mutex_init(&th->olock, NULL);
    pthread_cond_init(&th->cond, NULL);
    pthread_cond_init(&th->ocond, NULL);
    th->threads = calloc(th->nthreads, sizeof(*th->threads));

    for(i = 0; i < th->nthreads; i++){
	tcvp_pipe_t *te;

	th->threads[i].sh = player_new(cs, cs, t, NULL);
	th->threads[i].pipe = player_new_pipe(th->threads[i].sh, ms, s);
	pthread_mutex_init(&th->threads[i].lock, NULL);
	pthread_cond_init(&th->threads[i].cond, NULL);
	th->threads[i].end = tcallocz(sizeof(*th->threads[i].end));
	th->threads[i].end->input = th_input;
	th->threads[i].end->probe = th_probe;
	th->threads[i].end->flush = th_flush;
	th->threads[i].end->private = th->threads + i;
	th->threads[i].th = th;
	th->threads[i].pkq = calloc(th->qsize, sizeof(*th->threads[i].pkq));

	te = th->threads[i].pipe;
	while(te->next)
	    te = te->next;
	te->next = th->threads[i].end;
    }

    th->outq = calloc(th->osize, sizeof(*th->outq));

    th->pipe = p;
    p->private = th;

    return 0;
}
