/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

    Licensed under the Open Software License version 2.0
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
