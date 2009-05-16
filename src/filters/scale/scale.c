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
#include <tcmath.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <libswscale/swscale.h>
#include <scale_tc2.h>

typedef struct scale {
    struct SwsContext *sws;
    int w, h;
    int keepaspect;
    int sh;
} scale_t;

typedef struct scale_packet {
    tcvp_data_packet_t pk;
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
scale_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    scale_t *s = p->private;
    int i;

    if(pk->data){
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
	    op->data[i] = malloc(s->w * s->h / (d * d));
	    op->sizes[i] = s->w / d;
	}

	sws_scale(s->sws, pk->data, pk->sizes, 0, s->sh, op->data, op->sizes);

	tcfree(pk);
	pk = &op->pk;
    }

    p->next->input(p->next, (tcvp_packet_t *) pk);

    return 0;
}

extern int
scale_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    scale_t *sc = p->private;
    video_stream_t *vs = &p->format.video;

    if(!p->next)
	return PROBE_FAIL;

    sc->sws = sws_getContext(vs->width, vs->height, PIX_FMT_YUV420P,
                             sc->w, sc->h, PIX_FMT_YUV420P,
                             SWS_BILINEAR, NULL, NULL, NULL);
    sc->sh = vs->height;
    vs->width = sc->w;
    vs->height = sc->h;

    if(sc->keepaspect){
	vs->aspect.num = s->video.width;
	vs->aspect.den = s->video.height;
	tcreduce(&vs->aspect);
    }

    return PROBE_OK;
}

static void
scale_free(void *p)
{
    scale_t *s = p;

    if(s->sws)
	sws_freeContext(s->sws);
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

    tcconf_getvalue(cs, "keepaspect", "%i", &s->keepaspect);

    p->private = s;

    return 0;
}
