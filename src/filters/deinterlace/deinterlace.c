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
di_new(stream_t *s, conf_section *cs, timer__t *t)
{
    tcvp_pipe_t *p;

    p = tcallocz(sizeof(*p));
    p->format = *s;
    p->input = di_input;
    p->probe = di_probe;
    p->flush = di_flush;

    return p;
}
