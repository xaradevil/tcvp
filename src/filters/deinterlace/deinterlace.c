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

extern int
di_input(tcvp_pipe_t *p, packet_t *pk)
{
    int i;

    if(pk->data && p->format.video.flags & TCVP_STREAM_FLAG_INTERLACED){
	for(i = 0; i < pk->planes; i++)
	    pk->sizes[i] *= 2;
    }

    p->next->input(p->next, pk);

    return 0;
}

extern int
di_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    if(p->format.video.flags & TCVP_STREAM_FLAG_INTERLACED){
	p->format.video.height /= 2;
	p->format.video.flags &= ~TCVP_STREAM_FLAG_INTERLACED;
    }

    return 0;
}
