/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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

#include <stdio.h>
#include <stdlib.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

typedef struct mpeges {
    url_t *u;
    stream_t s;
    int used;
} mpeges_t;

typedef struct mpeges_packet {
    packet_t pk;
    int size;
    u_char *data;
} mpeges_packet_t;

static void
mpeges_free_pk(void *p)
{
    mpeges_packet_t *ep = p;
    free(ep->data);
}

static packet_t *
mpeges_packet(muxed_stream_t *ms, int str)
{
    mpeges_t *me = ms->private;
    mpeges_packet_t *ep;
    int size = 1024;
    u_char *buf = malloc(1024);

    size = me->u->read(buf, 1, 1024, me->u);
    if(size <= 0){
	free(buf);
	return NULL;
    }

    ep = tcallocdz(sizeof(*ep), NULL, mpeges_free_pk);
    ep->pk.data = &ep->data;
    ep->pk.sizes = &ep->size;
    ep->pk.planes = 1;
    ep->data = buf;
    ep->size = size;

    return &ep->pk;
}

static void
mpeges_free(void *p)
{
    muxed_stream_t *ms = p;
    mpeges_t *me = ms->private;
    if(me->u)
	tcfree(me->u);
}

extern muxed_stream_t *
mpeges_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    mpeges_t *me;
    char h[3] = {0, 0, 1}, b[3];

    u->read(b, 1, 3, u);
    u->seek(u, 0, SEEK_SET);
    if(memcmp(b, h, 3))
	return NULL;

    me = calloc(1, sizeof(*me));
    me->u = tcref(u);
    me->s.stream_type = STREAM_TYPE_VIDEO;
    me->s.video.codec = "video/mpeg";

    ms = tcallocdz(sizeof(*ms), NULL, mpeges_free);
    ms->n_streams = 1;
    ms->streams = &me->s;
    ms->used_streams = &me->used;
    ms->next_packet = mpeges_packet;
    ms->private = me;

    return ms;
}
