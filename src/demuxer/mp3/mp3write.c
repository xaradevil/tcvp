/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

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
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <mp3_tc2.h>
#include "id3.h"

typedef struct mp3_write {
    url_t *u;
    char *tag;
    int probed;
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
    mp3_write_t *mw = p->private;

    if(mw->probed)
	return PROBE_FAIL;

    mw->probed = 1;

    p->format = *s;
    p->format.stream_type = STREAM_TYPE_MULTIPLEX;
    p->format.common.codec = mw->tag;
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

static char *codecs[][2] = {
    { "audio/mpeg", "mp3" },
    { "audio/mp3", "mp3" },
    { "audio/mp2", "mp3" },
    { "audio/mp1", "mp3" },
    { "audio/aac", "aac" },
    { NULL, NULL }
};

extern tcvp_pipe_t *
mp3w_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    tcvp_pipe_t *p;
    mp3_write_t *mw;
    char *url;
    url_t *u;
    int i;

    if(tcconf_getvalue(cs, "mux/url", "%s", &url) <= 0)
	return NULL;

    for(i = 0; codecs[i][0]; i++){
	if(!strcmp(s->common.codec, codecs[i][0]))
	    break;
    }

    if(!codecs[i][0])
	return NULL;

    if(!(u = url_open(url, "w")))
	return NULL;

    id3v2_write_tag(u, ms);

    mw = calloc(1, sizeof(*mw));
    mw->u = u;
    mw->tag = codecs[i][1];

    p = tcallocdz(sizeof(*p), NULL, mp3w_free);
    p->input = mp3w_input;
    p->flush = mp3w_flush;
    p->probe = mp3w_probe;
    p->private = mw;

    return p;
}
