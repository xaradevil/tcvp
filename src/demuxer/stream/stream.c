/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
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
#define suffix_map tcvp_demux_stream_conf_suffix
#define suffix_map_size tcvp_demux_stream_conf_suffix_count

static int TCVP_STATE;

static void
cpattr(void *d, void *s, char *a)
{
    if(!tcattr_get(d, a)){
	void *v = tcattr_get(s, a);
	if(v){
	    tcattr_set(d, a, strdup(v), NULL, free);
	}
    }
}

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
	e = strcspn(m, " \t;");
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
	    for(i = 0; i < suffix_map_size; i++){
		if(!strcmp(s, suffix_map[i].suffix)){
		    m = strdup(suffix_map[i].demuxer);
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
    if(ms){
	char *a, *p;

	tcattr_set(ms, "file", strdup(name), NULL, free);
	cpattr(ms, u, "title");
	cpattr(ms, u, "performer");
	cpattr(ms, u, "artist");
	cpattr(ms, u, "album");

	a = tcattr_get(ms, "artist");
	p = tcattr_get(ms, "performer");

	if(!a && p)
	    tcattr_set(ms, "artist", strdup(p), NULL, free);
	if(a && !p)
	    tcattr_set(ms, "performer", strdup(a), NULL, free);
    }

    tcfree(u);

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

extern tcvp_pipe_t *
s_open_mux(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	   muxed_stream_t *ms)
{
    mux_new_t mnew = NULL;
    char *name, *sf;
    char *m = NULL;

    if(tcconf_getvalue(cs, "mux/url", "%s", &name) <= 0)
	return NULL;

    if((sf = strrchr(name, '.'))){
	int i;
	for(i = 0; i < suffix_map_size; i++){
	    if(!strcmp(sf, suffix_map[i].suffix)){
		m = suffix_map[i].muxer;
		break;
	    }
	}
    }

    if(m){
	char mb[strlen(m) + 5];
	sprintf(mb, "mux/%s", m);
	mnew = tc2_get_symbol(mb, "new");
    }

    free(name);
    return mnew? mnew(s, cs, t, ms): NULL;
}

#define RUN     1
#define PAUSE   2
#define STOP    3

#define min_buffer tcvp_demux_stream_conf_buffer

typedef struct s_play {
    int nms;
    tcvp_pipe_t **pipes;
    int nstreams, tstreams;
    pthread_t *threads, rth;
    sem_t rsm;
    int state;
    int flushing;
    int waiting;
    pthread_mutex_t mtx;
    pthread_cond_t cnd;
    eventq_t sq;
    struct sp_stream {
	muxed_stream_t *str;
	int soff;
	list *pq;
	sem_t ps;
	uint64_t pts;
    } *streams;
    int eof;
    uint64_t start_time, end_time;
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

    for(i = 0; i < vp->tstreams; i++){
	if(vp->streams[i].str &&
	   vp->streams[i].pts < pts &&
	   list_items(vp->streams[i].pq) < min_buffer){
	    s = i;
	    pts = vp->streams[i].pts;
	}
    }

    return s;
}

static void *
read_stream(void *_p)
{
    s_play_t *vp = _p;
    int s;

    for(;;){
	sem_wait(&vp->rsm);
	if(vp->state == STOP)
	    break;

	while((s = wstream(vp)) >= 0 && vp->state == RUN){
	    muxed_stream_t *ms = vp->streams[s].str;
	    packet_t *p = ms->next_packet(ms, s);
	    int str;

	    if(p){
		p->stream += vp->streams[s].soff;
		str = p->stream;
		if(p->flags & TCVP_PKT_FLAG_PTS)
		    vp->streams[str].pts = p->pts;
		list_push(vp->streams[str].pq, p);
		sem_post(&vp->streams[str].ps);
	    } else {
		vp->streams[s].str = NULL;
		sem_post(&vp->streams[s].ps);
		if(++vp->eof == vp->nms)
		    break;
	    }
	}
    }

    return NULL;
}

static packet_t *
get_packet(s_play_t *vp, int s)
{
    packet_t *p;

    if(vp->streams[s].str && sem_trywait(&vp->streams[s].ps)){
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
	if(!pk || (pk->flags & TCVP_PKT_FLAG_PTS && pk->pts > vp->end_time)){
	    if(pk)
		tcfree(pk);
	    break;
	}
	vp->pipes[str]->input(vp->pipes[str], pk);
    }

    pk = tcallocz(sizeof(*pk));
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

    for(i = 0; i < vp->tstreams; i++){
	if(vp->streams[i].str){
	    vp->pipes[i]->flush(vp->pipes[i], drop);
	    if(drop){
		freeq(vp, i);
		while(!sem_trywait(&vp->streams[i].ps));
	    }
	}
	vp->eof = 0;
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

    for(i = 0, j = 0; i < vp->tstreams; i++){
	if(vp->streams[i].str){
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
s_probe(s_play_t *vp, muxed_stream_t *ms, int msi, tcvp_pipe_t **codecs)
{
    uint64_t start_time = -1;
    int pcnt[ms->n_streams];
    int pstat[ms->n_streams];
    int stime[ms->n_streams];
    int spc = 0;
    int i;

    memset(pcnt, 0, ms->n_streams * sizeof(pcnt[0]));
    memset(pstat, 0, ms->n_streams * sizeof(pstat[0]));
    memset(stime, 0, ms->n_streams * sizeof(stime[0]));

    for(i = 0; i < ms->n_streams; i++){
	if(ms->used_streams[i])
	    spc++;
	vp->streams[msi + i].soff = msi;
	ms->streams[i].common.index += msi;
    }

    while(spc){
	packet_t *pk = ms->next_packet(ms, -1);
	int st, ci;

	if(!pk || !pk->data){
	    if(pk)
		tcfree(pk);
	    continue;
	}

	st = pk->stream;
	pk->stream += msi;
	ci = st + msi;

	if(ms->used_streams[st] && codecs[ci]->probe){
	    int ps;

	    if(!pstat[st] || pstat[st] == PROBE_AGAIN){
		tcref(pk);
		ps = codecs[ci]->probe(codecs[ci], pk, &ms->streams[st]);
		pcnt[st]++;
		pstat[st] = ps;
		if(ps != PROBE_AGAIN ||
		   pcnt[st] > tcvp_demux_stream_conf_max_probe){
		    spc--;
		}
		if(ps == PROBE_FAIL){
		    ms->used_streams[st] = 0;
		} else {
		    list_push(vp->streams[ci].pq, pk);
		    vp->streams[ci].str = ms;
		}
		if(!stime[st] && pk->flags & TCVP_PKT_FLAG_PTS){
		    ms->streams[st].common.start_time = pk->pts;
		    stime[st] = 1;
		    if(pk->pts < start_time)
			start_time = pk->pts;
		}
	    } else if(pstat[st] == PROBE_OK){
		list_push(vp->streams[st].pq, pk);
	    }
	}
    }

    if(start_time < vp->start_time)
	vp->start_time = start_time;

    for(i = 0; i < ms->n_streams; i++){
	int ci = msi + i;
	if(pstat[i] != PROBE_OK){
	    ms->used_streams[i] = 0;
	    freeq(vp, ci);
	    list_destroy(vp->streams[ci].pq, NULL);
	    memset(vp->streams + ci, 0, sizeof(*vp->streams));
	}
	if(codecs[ci])
	    codecs[ci]->flush(codecs[ci], 1);
    }

    return PROBE_OK;
}

extern tcvp_pipe_t *
s_play(muxed_stream_t **mss, int ns, tcvp_pipe_t **out, tcconf_section_t *cs)
{
    tcvp_pipe_t *p;
    s_play_t *vp;
    int i, j;
    char *qname, *qn;
    int time;
    int np = 0;

    for(i = 0; i < ns; i++)
	np += mss[i]->n_streams;

    vp = calloc(1, sizeof(*vp));
    vp->nms = ns;
    vp->tstreams = np;
    vp->pipes = calloc(np, sizeof(*vp->pipes));
    vp->threads = calloc(np, sizeof(*vp->threads));
    vp->streams = calloc(np, sizeof(*vp->streams));
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

    for(i = 0; i < np; i++){
	vp->streams[i].pq = list_new(TC_LOCK_SLOPPY);
	sem_init(&vp->streams[i].ps, 0, 0);
    }

    vp->start_time = -1;

    for(i = 0, j = 0; i < ns; i++){
	s_probe(vp, mss[i], j, out);
	j += mss[i]->n_streams;
    }

    pthread_create(&vp->rth, NULL, read_stream, vp);
    vp->state = PAUSE;

    if(tcconf_getvalue(cs, "play_time", "%i", &time) > 0){
	int start;
	if(tcconf_getvalue(cs, "start_time", "%i", &start) > 0)
	    time += start;
	vp->end_time = vp->start_time + time * 27000000LL;
    } else {
	vp->end_time = -1;
    }

    for(i = 0, j = 0; i < np; i++){
	if(vp->streams[i].str){
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
    file_magic = magic_open(MAGIC_SYMLINK | MAGIC_DEVICES);
    magic_load(file_magic, DATADIR "/magic");
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
