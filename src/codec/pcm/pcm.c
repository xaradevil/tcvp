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
    return p->next->probe(p->next, NULL, s);
}

extern tcvp_pipe_t *
pcm_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t **t)
{
    tcvp_pipe_t *np = tcallocz(sizeof(*np));
    np->input = pcm_input;
    np->flush = pcm_flush;
    np->probe = pcm_probe;

    return np;
}
