/**
    Copyright (C) 2003-2005  Michael Ahlberg, Måns Rullgård

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
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <mp3_tc2.h>
#include "id3.h"

typedef struct mp3_write {
    url_t *u;
    char *tag;
    int probed;
} mp3_write_t;

extern int
mp3w_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    mp3_write_t *mw = p->private;

    if(pk->data)
        mw->u->write(pk->data[0], 1, pk->sizes[0], mw->u);

    tcfree(pk);
    return 0;
}

extern int
mp3w_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
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
    mp3_write_t *mw = p;
    mw->u->close(mw->u);
}

static struct {
    char *codec;
    char *tag;
    int id3;
} codecs[] = {
    { "audio/mpeg", "mp3", 1 },
    { "audio/mp3", "mp3", 1 },
    { "audio/mp2", "mp3", 1 },
    { "audio/mp1", "mp3", 1 },
    { "audio/aac", "aac", 1 },
    { "audio/ac3", "ac3", 0 },
    { "audio/dts", "dts", 0 },
    { NULL, NULL }
};

extern int
mp3w_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
         muxed_stream_t *ms)
{
    mp3_write_t *mw;
    char *url = NULL;
    url_t *u;
    int i, ret = -1;

    if(tcconf_getvalue(cs, "mux/url", "%s", &url) <= 0)
        return -1;

    for(i = 0; codecs[i].codec; i++){
        if(!strcmp(s->common.codec, codecs[i].codec))
            break;
    }

    if(!codecs[i].codec)
        goto out;

    if(!(u = url_open(url, "w")))
        goto out;

    if(codecs[i].id3)
        id3v2_write_tag(u, ms);

    mw = tcallocdz(sizeof(*mw), NULL, mp3w_free);
    mw->u = u;
    mw->tag = codecs[i].tag;

    p->private = mw;

    ret = 0;

  out:
    free(url);
    return ret;
}
