/**
    Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

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
#include <tctypes.h>
#include <tcalloc.h>
#include <ogg/ogg.h>
#include <ogg_tc2.h>

typedef struct ogg_write {
    url_t *out;
    ogg_stream_state os;
} ogg_write_t;

static int
ow_write_page(ogg_write_t *ow, ogg_page *op)
{
    ow->out->write(op->header, 1, op->header_len, ow->out);
    ow->out->write(op->body, 1, op->body_len, ow->out);
    return 0;
}

extern int
ow_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    ogg_write_t *ow = p->private;
    ogg_page opg;

    if(pk->data){
	ogg_packet *op = (ogg_packet *) pk->data[0];

	ogg_stream_packetin(&ow->os, op);
	while(ogg_stream_pageout(&ow->os, &opg))
	    ow_write_page(ow, &opg);
    } else {
	if(ogg_stream_flush(&ow->os, &opg))
	    ow_write_page(ow, &opg);
    }

    tcfree(pk);

    return 0;
}

extern int
ow_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    ogg_write_t *ow = p->private;
    ogg_packet *op = (ogg_packet *) pk->data[0];
    ogg_page opg;
    int ret;

    if(op->packetno == 0){
	p->format = *s;
	p->format.stream_type = STREAM_TYPE_MULTIPLEX;
	p->format.common.codec = "ogg";
    }

    ogg_stream_packetin(&ow->os, op);

    if(op->packetno == 2){
	while(ogg_stream_flush(&ow->os, &opg))
	    ow_write_page(ow, &opg);
    }

    ret = op->packetno < 2? PROBE_AGAIN: PROBE_OK;
    tcfree(pk);
    return ret;
}

static void
ow_free(void *p)
{
    ogg_write_t *ow = p;

    ogg_stream_clear(&ow->os);
    ow->out->close(ow->out);
}

extern int
ow_new(tcvp_pipe_t *tp, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
       muxed_stream_t *ms)
{
    ogg_write_t *ow;
    char *url;
    url_t *out;

    if(tcconf_getvalue(cs, "mux/url", "%s", &url) <= 0)
	return -1;
    if(!(out = url_open(url, "w")))
	return -1;

    ow = tcallocdz(sizeof(*ow), NULL, ow_free);
    ow->out = out;
    ogg_stream_init(&ow->os, 0);

    tp->private = ow;

    return 0;
}
