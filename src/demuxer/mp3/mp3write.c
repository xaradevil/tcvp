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
#include <tcalloc.h>
#include <tcbyteswap.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <mp3_tc2.h>

typedef struct mp3_write {
    url_t *u;
} mp3_write_t;

static int
mp3w_input(tcvp_pipe_t *p, packet_t *pk)
{
    mp3_write_t *mw = p->private;

    if(pk->data)
	mw->u->write(pk->data[0], 1, pk->sizes[0], mw->u);

    tcfree(pk);
    return 0;
}

static int
mp3w_flush(tcvp_pipe_t *p, int drop)
{
    return 0;
}

static int
mp3w_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    p->format = *s;
    p->format.stream_type = STREAM_TYPE_MULTIPLEX;
    p->format.common.codec = "mp3";
    return PROBE_OK;
}

static void
mp3w_free(void *p)
{
    tcvp_pipe_t *tp = p;
    mp3_write_t *mw = tp->private;
    mw->u->close(mw->u);
    free(mw);
}

extern tcvp_pipe_t *
mp3w_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    tcvp_pipe_t *p;
    mp3_write_t *mw;
    char *url;
    url_t *u;

    if(tcconf_getvalue(cs, "mux/url", "%s", &url) <= 0)
	return NULL;

    if(!(u = url_open(url, "w")))
	return NULL;

    mw = calloc(1, sizeof(*mw));
    mw->u = u;

    p = tcallocdz(sizeof(*p), NULL, mp3w_free);
    p->input = mp3w_input;
    p->flush = mp3w_flush;
    p->probe = mp3w_probe;
    p->private = mw;

    return p;
}
