/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <tcalloc.h>
#include <tcvp_types.h>
#include <pcm_tc2.h>

static int
pcm_input(tcvp_pipe_t *p, packet_t *pk)
{
    return p->next->input(p->next, pk);
}

static int
pcm_flush(tcvp_pipe_t *p, int drop)
{
    return p->next->flush(p->next, drop);
}

static int
pcm_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    p->format = *s;
    return p->next->probe(p->next, NULL, s);
}

extern tcvp_pipe_t *
pcm_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    tcvp_pipe_t *np = tcallocz(sizeof(*np));
    np->input = pcm_input;
    np->flush = pcm_flush;
    np->probe = pcm_probe;

    return np;
}
