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
#include <tclist.h>
#include <pthread.h>
#include <tcvp.h>
#include <pcm_tc2.h>

static int
pcm_free_pipe(tcvp_pipe_t *p)
{
    free(p);
    return 0;
}

extern tcvp_pipe_t *
pcm_new_pipe(int mode, tcvp_pipe_t *p)
{
    tcvp_pipe_t *np = malloc(sizeof(*np));
    np->input = p->input;
    np->start = p->start;
    np->stop = p->stop;
    np->free = pcm_free_pipe;
    np->private = p->private;

    return np;
}

extern packet_t *
pcm_encdec(codec_t *c, packet_t *p)
{
    return p;
}

static int
pcm_free(codec_t *c)
{
    free(c);
    return 0;
}

extern codec_t *
pcm_new(int mode)
{
    codec_t *c = malloc(sizeof(*c));
    c->decode = pcm_encdec;
    c->encode = pcm_encdec;
    c->free = pcm_free;
    return c;
}
