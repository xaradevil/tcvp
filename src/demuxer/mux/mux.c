/**
    Copyright (C) 2004  Michael Ahlberg, M�ns Rullg�rd

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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <mux_tc2.h>

typedef struct mux {
    int nstreams;
    int waiting;
    struct {
	uint64_t time;
	int rate;
    } *streams;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} mux_t;

static int
next_stream(mux_t *mx)
{
    uint64_t t = -1LL;
    int i, s = -1;

    for(i = 0; i < mx->nstreams; i++){
	if(mx->streams[i].time < t){
	    t = mx->streams[i].time;
	    s = i;
	}
    }

    return s;
}

static int
mux_packet(tcvp_pipe_t *tp, packet_t *pk)
{
    mux_t *mx = tp->private;

    pthread_mutex_lock(&mx->lock);

    if(pk->flags & TCVP_PKT_FLAG_DTS)
	mx->streams[pk->stream].time = pk->dts;
    else if(pk->flags & TCVP_PKT_FLAG_PTS)
	mx->streams[pk->stream].time = pk->pts;

/*     tc2_print("MUX", TC2_PRINT_DEBUG, */
/* 	      "%i time %llu\n", pk->stream, mx->streams[pk->stream].time); */

    mx->waiting++;
    pthread_cond_broadcast(&mx->cond);

    while(mx->waiting < mx->nstreams && next_stream(mx) != pk->stream)
	pthread_cond_wait(&mx->cond, &mx->lock);

    mx->waiting--;
    if(!pk->data)
	mx->nstreams--;

    if(pk->data)
	mx->streams[pk->stream].time +=
	    pk->sizes[0] * mx->streams[pk->stream].rate;

    tp->next->input(tp->next, pk);

    pthread_cond_broadcast(&mx->cond);
    pthread_mutex_unlock(&mx->lock);
    return 0;
}

static int
mux_probe(tcvp_pipe_t *tp, packet_t *pk, stream_t *s)
{
    mux_t *mx = tp->private;

    tc2_print("MUX", TC2_PRINT_DEBUG, "probe %i\n", pk->stream);
	mx->streams[pk->stream].rate =
	    27000000LL * 8 / (s->common.bit_rate? s->common.bit_rate: 320000);
    return tp->next->probe(tp->next, pk, s);
}

static int
mux_flush(tcvp_pipe_t *tp, int d)
{
    tc2_print("MUX", TC2_PRINT_DEBUG, "flush %i\n", d);
    return tp->next->flush(tp->next, d);
}

static void
mux_ref(void *p)
{
    tcvp_pipe_t *tp = p;
    mux_t *mx = tp->private;

    pthread_mutex_lock(&mx->lock);
    mx->nstreams++;
    tc2_print("MUX", TC2_PRINT_DEBUG, "ref, nstreams=%i\n", mx->nstreams);
    mx->streams = realloc(mx->streams, mx->nstreams * sizeof(*mx->streams));
    memset(mx->streams + mx->nstreams - 1, 0, sizeof(*mx->streams));
    pthread_mutex_unlock(&mx->lock);
}

static void
mux_free(void *p)
{
    tcvp_pipe_t *tp = p;
    mux_t *mx = tp->private;

    free(mx->streams);
    pthread_mutex_destroy(&mx->lock);
    pthread_cond_destroy(&mx->cond);
    free(mx);
}

extern tcvp_pipe_t *
mux_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    tcvp_pipe_t *tp;
    mux_t *mx;

    mx = calloc(1, sizeof(*mx));
    pthread_mutex_init(&mx->lock, NULL);
    pthread_cond_init(&mx->cond, NULL);

    tp = tcallocdz(sizeof(*tp), mux_ref, mux_free);
    tp->format = *s;
    tp->input = mux_packet;
    tp->flush = mux_flush;
    tp->probe = mux_probe;
    tp->private = mx;

    return tp;
}