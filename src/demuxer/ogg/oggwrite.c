/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
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

static int
ow_input(tcvp_pipe_t *p, packet_t *pk)
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

static int
ow_flush(tcvp_pipe_t *p, int drop)
{
    return 0;
}

static int
ow_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
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
    tcvp_pipe_t *tp = p;
    ogg_write_t *ow = tp->private;

    ogg_stream_clear(&ow->os);
    ow->out->close(ow->out);
    free(ow);
}

extern tcvp_pipe_t *
ow_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    tcvp_pipe_t *tp;
    ogg_write_t *ow;
    char *url;
    url_t *out;

    if(tcconf_getvalue(cs, "mux/url", "%s", &url) <= 0)
	return NULL;
    if(!(out = url_open(url, "w")))
	return NULL;

    ow = calloc(1, sizeof(*ow));
    ow->out = out;
    ogg_stream_init(&ow->os, 0);


    tp = tcallocdz(sizeof(*tp), NULL, ow_free);
    tp->input = ow_input;
    tp->flush = ow_flush;
    tp->probe = ow_probe;
    tp->private = ow;

    return tp;
}
