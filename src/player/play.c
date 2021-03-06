/**
    Copyright (C) 2004-2005  Michael Ahlberg, Måns Rullgård

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
#include <tcstring.h>
#include <tctypes.h>
#include <pthread.h>
#include <tcalloc.h>
#include <tclist.h>
#include <tchash.h>
#include <tcvp_types.h>
#include <player_tc2.h>

#define RUN   1
#define STOP  2
#define PAUSE 3

#define buffertime (tcvp_player_conf_buffer * 27000)
#define min_packets tcvp_player_conf_min_packets
#define max_packets tcvp_player_conf_max_packets

struct tcvp_player {
    int sid;
    int as, vs, ss;
    uint64_t starttime, endtime, playtime;
    tcconf_section_t *conf, *profile;
    tchash_table_t *filters;
    tcvp_timer_t *timer;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    eventq_t sq;
    char *outfile;
    int nstreams, nready;
    int synctime;
};

typedef struct stream_play {
    muxed_stream_t *ms;
    struct sp_stream {
        tcvp_pipe_t *pipe, *end;
        tclist_t *packets;
        uint64_t starttime;
        uint64_t headtime, tailtime;
        int probe, nprobe;
        pthread_t th;
        int run;
        struct stream_play *sp;
    } *streams;
    int *smap;
    int nstreams, pstreams;
    int fail;
    int waiting;
    uint64_t nbuf;
    int state;
    pthread_t rth;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    tcvp_player_t *shared;
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

extern void
close_pipe(tcvp_pipe_t *p)
{
    while(p){
        tcvp_pipe_t *np = p->next;
        tcfree(p);
        p = np;
    }
}

static void
pid_free(void *p)
{
    tcvp_player_t *sh = tcattr_get(p, "stream-shared");
    tc2_print("STREAM", TC2_PRINT_DEBUG, "removing %s from hash\n", p);
    tchash_delete(sh->filters, p, -1, NULL);
    tcfree(p);
}

extern tcvp_pipe_t *
new_pipe(tcvp_player_t *sh, muxed_stream_t *ms, stream_t *s)
{
    tcvp_pipe_t *pipe = NULL, *pp = NULL, *pn = NULL;
    tcconf_section_t *f, *mcf;
    tcconf_section_t *pr = NULL;
    void *cs = NULL;
    int skip = 0;

    switch(s->stream_type){
    case STREAM_TYPE_VIDEO:
        pr = tcconf_getsection(sh->profile, "video");
        break;
    case STREAM_TYPE_AUDIO:
        pr = tcconf_getsection(sh->profile, "audio");
        break;
    case STREAM_TYPE_SUBTITLE:
        pr = tcconf_getsection(sh->profile, "subtitle");
        break;
    }

    if(!pr)
        return NULL;

    while((f = tcconf_nextsection(pr, "filter", &cs))){
        char *type, *id = NULL;
        filter_new_t fn;

        if(skip){
            tcfree(f);
            continue;
        }

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

            if(!(pn = fn(pp? &pp->format: s, mcf, sh->timer, ms))){
                tc2_print("STREAM", TC2_PRINT_WARNING,
                          "error opening filter '%s'\n", type);
                break;
            }

            if(id){
                char *cid = tcalloc(strlen(id) + 1);
                strcpy(cid, id);
                tcattr_set(cid, "stream-shared", sh, NULL, NULL);
                tcattr_set(pn, "id", cid, NULL, pid_free);
                tchash_replace(sh->filters, id, -1, pn, NULL);
            }
            tcfree(mcf);
        } else {
            tcref(pn);
        }

        if(id)
            free(id);

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
            skip = 1;
        }
    }

    if(!pn){
        close_pipe(pipe);
        pipe = NULL;
    }

    tcfree(pr);
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
use_stream(tcvp_player_t *sh, int s, stream_t *str)
{
    int t = str->stream_type;
    void *cs = NULL;
    int ss, u = 0, shs;
    char *c;
    char *lang = NULL, *clang = NULL;
    int program = -1;

    if(t == STREAM_TYPE_AUDIO){
        c = "audio/stream";
        lang = str->audio.language;
        shs = sh->as;
    } else if(t == STREAM_TYPE_VIDEO){
        c = "video/stream";
        shs = sh->vs;
    } else if(t == STREAM_TYPE_SUBTITLE){
        c = "subtitle/stream";
        lang = str->subtitle.language;
        shs = sh->ss;
    } else {
        return 0;
    }

    if(tcconf_getvalue(sh->conf, c, "")){
        if(tcconf_getvalue(sh->conf, "program", "%i", &program) > 0)
            if(program != str->common.program)
                return 0;
        switch(t){
        case STREAM_TYPE_VIDEO:
            return sh->vs < 0;
        case STREAM_TYPE_AUDIO:
            return sh->as < 0;
        case STREAM_TYPE_SUBTITLE:
            return 0;
        default:
            return 0;
        }
    }

    while(tcconf_nextvalue(sh->conf, c, &cs, "%i%s", &ss, &clang) > 0){
        if(clang){
            if(lang && shs < 0)
                u = !strcmp(lang, clang);
            free(clang);
            clang = NULL;
        } else {
            u = ss == s;
        }
    }

    return u;
}

static int
add_stream(stream_player_t *sp, int s)
{
    tcvp_player_t *sh = sp->shared;
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

    if(!use_stream(sh, sid, sp->ms->streams + s))
        goto out;
    r = -2;

    if(sp->ms->streams[s].stream_type == STREAM_TYPE_VIDEO){
        sh->vs = sid;
    } else if(sp->ms->streams[s].stream_type == STREAM_TYPE_AUDIO){
        sh->as = sid;
    } else if(sp->ms->streams[s].stream_type == STREAM_TYPE_SUBTITLE){
        sh->ss = sid;
    }

    tc2_print("STREAM", TC2_PRINT_DEBUG,
              "creating pipeline for stream #%i\n", sid);
    if(!(tp = new_pipe(sh, sp->ms, sp->ms->streams + s)))
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
    if(!(sp->ms->streams[s].common.flags & TCVP_STREAM_FLAG_NOBUFFER))
        sp->nbuf |= 1ULL << s;

    r = 0;

out:
    pthread_mutex_unlock(&sh->lock);
    if(r < 0)
        sp->fail++;
    if(r < -1)
        tc2_print("STREAM", TC2_PRINT_WARNING,
                  "error opening stream #%i\n", sid);
    return r;
}

static int
del_stream(stream_player_t *sp, int s)
{
    tcvp_player_t *sh = sp->shared;
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
    sp->nbuf &= ~(1ULL << s);

    if(sp->fail == sp->ms->n_streams){
        tcvp_event_send(sh->sq, TCVP_STATE, TCVP_STATE_ERROR);
    }

    if(str->sp){
        if(!--sp->pstreams){
            pthread_mutex_lock(&sh->lock);
            if(!--sh->nstreams)
                tcvp_event_send(sh->sq, TCVP_STATE, TCVP_STATE_END);
            pthread_mutex_unlock(&sh->lock);
            sp->state = STOP;
        }
    }

    pthread_cond_broadcast(&sp->cond);
    pthread_mutex_unlock(&sp->lock);

    return 0;
}

static int
flush_stream(stream_player_t *sp, int sx, int drop)
{
    struct sp_stream *str = sp->streams + sx;
    tcvp_packet_t *pk;

    if(!str->packets)
        return 0;

    if(drop)
        while((pk = tclist_shift(str->packets)))
            tcfree(pk);

    if(str->pipe)
        str->pipe->flush(str->pipe, drop);

    if(str->probe == PROBE_OK)
        if(!(sp->ms->streams[sx].common.flags & TCVP_STREAM_FLAG_NOBUFFER))
            sp->nbuf |= 1ULL << sx;

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
    tcvp_packet_t *pk;

    tc2_print("STREAM", TC2_PRINT_DEBUG,
              "[%i] starting player thread\n", shs);

    while(waitplay(sp, six)){
        pthread_mutex_lock(&sp->lock);
        pk = tclist_shift(str->packets);

        if(pk && pk->type == TCVP_PKT_TYPE_DATA &&
           pk->data.flags & TCVP_PKT_FLAG_PTS)
            str->tailtime = pk->data.pts;

        if((tclist_items(str->packets) < min_packets ||
            str->headtime - str->tailtime < buffertime) &&
           sp->ms->used_streams[six]){
            if(!(sp->ms->streams[six].common.flags &
                 TCVP_STREAM_FLAG_NOBUFFER))
                sp->nbuf |= 1ULL << six;
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
    pk->data.stream = shs;
    pk->data.data = NULL;
    if(str->end->start)
        str->end->start(str->end);
    str->pipe->flush(str->pipe, sp->state == STOP);
    str->pipe->input(str->pipe, pk);

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

static int
do_data_packet(stream_player_t *sp, tcvp_data_packet_t *pk)
{
    tcvp_player_t *sh = sp->shared;
    struct sp_stream *str;
    int ps;

    ps = pk->stream;

    if(pk->stream >= sp->nstreams || sp->smap[ps] < 0){
        if(add_stream(sp, ps)){
            del_stream(sp, ps);
            return -1;
        }
    }

    str = sp->streams + ps;
    pk->stream = sp->smap[ps];

    if(pk->flags & TCVP_PKT_FLAG_PTS){
        if(sh->starttime == -1LL){
            pthread_mutex_lock(&sh->lock);
            if(sh->starttime == -1LL){
                sh->starttime = pk->pts;
                if(sh->playtime != -1LL)
                    sh->endtime = sh->starttime + sh->playtime;
                sh->timer->reset(sh->timer, sh->starttime);
                tc2_print("STREAM", TC2_PRINT_DEBUG, "start %llu, end %llu\n",
                          sh->starttime / 27, sh->endtime / 27);
            }
            pthread_mutex_unlock(&sh->lock);
        }

        if(pk->pts > sh->endtime){
            tc2_print("STREAM", TC2_PRINT_DEBUG,
                      "[%i] end time reached\n", pk->stream);
            tcfree(pk);
            pk = NULL;
            pthread_mutex_lock(&sp->lock);
            sp->ms->used_streams[ps] = 0;
            sp->nbuf &= ~(1ULL << ps);
            pthread_cond_broadcast(&sp->cond);
            pthread_mutex_unlock(&sp->lock);
        } else if(str->starttime == -1LL){
            tc2_print("STREAM", TC2_PRINT_DEBUG,
                      "[%i] start %llu\n",
                      pk->stream, pk->pts / 27);
            sp->ms->streams[ps].common.start_time = pk->pts;
            str->starttime = pk->pts;
        }
/*     } else if(str->starttime == -1){ */
/*      tcfree(pk); */
/*      return 0; */
    }

    switch(str->probe){
    case PROBE_AGAIN:
    case PROBE_DISCARD:
        tc2_print("STREAM", TC2_PRINT_DEBUG, "[%i] probing\n",
                  pk->stream);
        sp->ms->streams[ps].common.index = pk->stream;
        str->probe = str->pipe->probe(str->pipe, pk,
                                      sp->ms->streams + ps);
        if(str->probe == PROBE_FAIL ||
           str->nprobe++ > tcvp_player_conf_max_probe){
            tc2_print("STREAM", TC2_PRINT_DEBUG,
                      "[%i] failed probe\n", pk->stream);
            sp->fail++;
            del_stream(sp, ps);
            tcfree(pk);
            break;
        } else if(str->probe == PROBE_OK){
            stream_time(sp->ms, ps, str->pipe);
            tcvp_event_send(sh->sq, TCVP_LOAD, sp->ms);
            pthread_create(&str->th, NULL, play_stream, str);
            pthread_mutex_lock(&sp->lock);
            if(str->end->start && str->run)
                str->end->start(str->end);
            pthread_mutex_unlock(&sp->lock);
        } else if(str->probe == PROBE_DISCARD){
            flush_stream(sp, ps, 1);
            tcfree(pk);
            break;
        }
    case PROBE_OK:
        pthread_mutex_lock(&sp->lock);
        if(str->packets){
            int np;

            tclist_push(str->packets, pk);
            if(pk && pk->flags & TCVP_PKT_FLAG_PTS)
                str->headtime = pk->pts;

            np = tclist_items(str->packets);
            if(str->probe == PROBE_OK && (np > max_packets ||
               ((str->headtime - str->tailtime > buffertime) &&
                np > min_packets)))
                sp->nbuf &= ~(1ULL << ps);
        }
        pthread_cond_broadcast(&sp->cond);
        pthread_mutex_unlock(&sp->lock);
        break;
    }

    return 0;
}

static void *
read_stream(void *p)
{
    stream_player_t *sp = p;

    tc2_print("STREAM", TC2_PRINT_DEBUG, "read_stream starting\n");

    while(waitbuf(sp)){
        tcvp_packet_t *tpk = NULL;

        if(sp->pstreams)
            tpk = (tcvp_packet_t *) sp->ms->next_packet(sp->ms, -1);

        if(!tpk){
            int i;

            tc2_print("STREAM", TC2_PRINT_DEBUG, "end of stream\n");

            pthread_mutex_lock(&sp->lock);
            for(i = 0; i < sp->nstreams; i++)
                if(sp->streams[i].packets)
                    tclist_push(sp->streams[i].packets, NULL);
            pthread_cond_broadcast(&sp->cond);
            pthread_mutex_unlock(&sp->lock);
            break;
        }

        switch(tpk->type){
        case TCVP_PKT_TYPE_DATA:
            do_data_packet(sp, &tpk->data);
            break;
        case TCVP_PKT_TYPE_FLUSH:
            if(tpk->flush.stream < 0){
                int i;
                for(i = 0; i < sp->nstreams; i++)
                    flush_stream(sp, i, tpk->flush.discard);
            } else {
                flush_stream(sp, tpk->flush.stream, tpk->flush.discard);
            }
            tcfree(tpk);
            break;
        case TCVP_PKT_TYPE_STILL:
            tc2_print("STREAM", TC2_PRINT_DEBUG, "still\n");
            tcfree(tpk);
            break;
        case TCVP_PKT_TYPE_TIMER:
            if(sp->shared->synctime)
                sp->shared->timer->reset(sp->shared->timer, tpk->timer.time);
            tcfree(tpk);
            break;
        }
    }

    pthread_mutex_lock(&sp->lock);
    sp->nbuf = 0;
    pthread_cond_broadcast(&sp->cond);
    pthread_mutex_unlock(&sp->lock);

    tc2_print("STREAM", TC2_PRINT_DEBUG, "read_stream done\n");

    return NULL;
}

static int
s_start(tcvp_pipe_t *tp)
{
    stream_player_t *sp = tp->private;
    tcvp_player_t *sh = sp->shared;
    int i;

    tc2_print("STREAM", TC2_PRINT_DEBUG, "start\n");

    pthread_mutex_lock(&sp->lock);
    sp->state = RUN;
    pthread_cond_broadcast(&sp->cond);

    tc2_print("STREAM", TC2_PRINT_DEBUG, "buffering\n");
    while(sp->nbuf)
        pthread_cond_wait(&sp->cond, &sp->lock);

    for(i = 0; i < sp->nstreams; i++){
        if(sp->streams[i].probe == PROBE_OK &&
           sp->streams[i].end && sp->streams[i].end->start){
            tc2_print("STREAM", TC2_PRINT_DEBUG, "starting player %i\n", i);
            sp->streams[i].end->start(sp->streams[i].end);
            tc2_print("STREAM", TC2_PRINT_DEBUG, "started player %i\n", i);
        }
        sp->streams[i].run = 1;
    }
    pthread_mutex_unlock(&sp->lock);

    pthread_mutex_lock(&sh->lock);
    sh->nready++;
    if(sh->nready == sh->nstreams){
        tc2_print("STREAM", TC2_PRINT_DEBUG, "starting timer\n");
        sh->timer->start(sh->timer);
    }
    pthread_mutex_unlock(&sh->lock);

    return 0;
}

static int
s_stop(tcvp_pipe_t *tp)
{
    stream_player_t *sp = tp->private;
    tcvp_player_t *sh = sp->shared;
    int i;

    tc2_print("STREAM", TC2_PRINT_DEBUG, "stop\n");

    pthread_mutex_lock(&sp->lock);
    sp->state = PAUSE;
    tc2_print("STREAM", TC2_PRINT_DEBUG, "waiting for player threads\n");
    while(sp->waiting < sp->pstreams)
        pthread_cond_wait(&sp->cond, &sp->lock);
    for(i = 0; i < sp->nstreams; i++){
        if(sp->streams[i].probe == PROBE_OK &&
           sp->streams[i].end && sp->streams[i].end->stop)
            sp->streams[i].end->stop(sp->streams[i].end);
        sp->streams[i].run = 0;
    }
    pthread_mutex_unlock(&sp->lock);

    pthread_mutex_lock(&sh->lock);
    sh->nready--;
    if(!sh->nready){
        tc2_print("STREAM", TC2_PRINT_DEBUG, "stopping timer\n");
        sh->timer->stop(sh->timer);
    }
    pthread_mutex_unlock(&sh->lock);

    return 0;
}

static int
s_flush(tcvp_pipe_t *tp, int drop)
{
    stream_player_t *sp = tp->private;
    int i;

    tc2_print("STREAM", TC2_PRINT_DEBUG, "flushing, drop=%i\n", drop);

    for(i = 0; i < sp->nstreams; i++){
        flush_stream(sp, i, drop);
/*      if(sp->streams[i].probe == PROBE_OK) */
/*          sp->nbuf |= 1 << i; */
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
    free(sp->smap);
    free(sp);
}

extern tcvp_pipe_t *
s_play(tcvp_player_t *sh, muxed_stream_t *ms)
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
        if(add_stream(sp, i))
            del_stream(sp, i);

    if(!sp->pstreams)
        return NULL;            /* FIXME: leak */

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
sh_free(void *p)
{
    tc2_print("STREAM", TC2_PRINT_WARNING, "%p still in hash\n", p);
}

static void
free_shared(void *p)
{
    tcvp_player_t *sh = p;

    tcfree(sh->profile);
    tcfree(sh->conf);
    tcfree(sh->timer);
    tchash_destroy(sh->filters, sh_free);
    if(sh->outfile)
        free(sh->outfile);
}

extern tcvp_player_t *
new_player(tcconf_section_t *profile, tcconf_section_t *conf,
           tcvp_timer_t *timer, char *out)
{
    tcvp_player_t *sh;
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
    sh->ss = -1;
    sh->starttime = -1LL;
    sh->endtime = -1LL;
    sh->playtime = -1LL;
    if(out)
        sh->outfile = strdup(out);

    if(tcconf_getvalue(conf, "play_time", "%i", &pt) > 0)
        sh->playtime = pt * 27000000LL;

    sh->synctime = tcvp_player_conf_synctime;
    tcconf_getvalue(profile, "synctime", "%i", &sh->synctime);

    sh->sq = tcvp_event_get_sendq(conf, "status");

    return sh;
}
