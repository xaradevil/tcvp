/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcmath.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <ffmpeg/avcodec.h>
#include <scale_tc2.h>

typedef struct scale {
    ImgReSampleContext *irs;
    int w, h;
} scale_t;

typedef struct scale_packet {
    packet_t pk;
    u_char *data[4];
    int sizes[4];
} scale_packet_t;

void
scale_free_pk(void *p)
{
    scale_packet_t *sp = p;
    int i;

    for(i = 0; i < 3; i++)
	free(sp->data[i]);
}

extern int
scale_input(tcvp_pipe_t *p, packet_t *pk)
{
    scale_t *s = p->private;
    int i;

    if(pk->data){
	AVPicture in, out;
	scale_packet_t *op;

	op = tcallocdz(sizeof(*op), NULL, scale_free_pk);
	op->pk.stream = pk->stream;
	op->pk.data = op->data;
	op->pk.sizes = op->sizes;
	op->pk.planes = 3;
	op->pk.flags = pk->flags;
	op->pk.pts = pk->pts;
	op->pk.dts = pk->dts;

	for(i = 0; i < 3; i++){
	    int d = i? 2: 1;

	    in.data[i] = pk->data[i];
	    in.linesize[i] = pk->sizes[i];

	    op->data[i] = out.data[i] = malloc(s->w * s->h / (d * d));
	    op->sizes[i] = out.linesize[i] = s->w / d;
	}

	img_resample(s->irs, &out, &in);

	tcfree(pk);
	pk = &op->pk;
    }

    p->next->input(p->next, pk);

    return 0;
}

extern int
scale_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    scale_t *sc = p->private;
    video_stream_t *vs = &p->format.video;

    if(!p->next)
	return PROBE_FAIL;

    sc->irs = img_resample_init(sc->w, sc->h, vs->width, vs->height);
    vs->width = sc->w;
    vs->height = sc->h;

    return PROBE_OK;
}

static void
scale_free(void *p)
{
    scale_t *s = p;

    if(s->irs)
	img_resample_close(s->irs);
}

extern int
scale_new(tcvp_pipe_t *p, stream_t *st, tcconf_section_t *cs, tcvp_timer_t *t,
	  muxed_stream_t *ms)
{
    scale_t *s;
    int w, h;

    tcconf_getvalue(cs, "width", "%i", &w);
    tcconf_getvalue(cs, "height", "%i", &h);

    if(!(w && h))
	return -1;

    s = tcallocdz(sizeof(*s), NULL, scale_free);
    s->w = w;
    s->h = h;

    p->private = s;

    return 0;
}
