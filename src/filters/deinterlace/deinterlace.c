/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <deinterlace_tc2.h>

static int
di_input(tcvp_pipe_t *p, packet_t *pk)
{
    int i;

    if(pk->data && p->private){
	for(i = 0; i < pk->planes; i++)
	    pk->sizes[i] *= 2;
    }

    p->next->input(p->next, pk);

    return 0;
}

static int
di_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    p->format = *s;
    if(p->format.video.flags & TCVP_STREAM_FLAG_INTERLACED){
	p->format.video.height /= 2;
	p->private = (void *) 1L;
	p->format.video.flags &= ~TCVP_STREAM_FLAG_INTERLACED;
    }

    return p->next->probe(p->next, pk, &p->format);
}

static int
di_flush(tcvp_pipe_t *p, int drop)
{
    return p->next->flush(p->next, drop);
}

extern tcvp_pipe_t *
di_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    tcvp_pipe_t *p;

    p = tcallocz(sizeof(*p));
    p->format = *s;
    p->input = di_input;
    p->probe = di_probe;
    p->flush = di_flush;

    return p;
}
