/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcmath.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <crop_tc2.h>

typedef struct crop {
    int x, y, w, h;
} crop_t;

extern int
crop_input(tcvp_pipe_t *p, packet_t *pk)
{
    crop_t *c = p->private;
    int i;

    if(pk->data){
	for(i = 0; i < pk->planes; i++){
	    int d = i? 2: 1;
	    pk->data[i] += pk->sizes[i] * c->y / d + c->x / d;
	}
    }

    p->next->input(p->next, pk);

    return 0;
}

extern int
crop_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    crop_t *c = p->private;
    video_stream_t *vs = &p->format.video;
    tcfraction_t sar = { 1, 1 };

    if(vs->aspect.num){
	sar.num = vs->height * vs->aspect.num;
	sar.den = vs->width * vs->aspect.den;
	tcreduce(&sar);
    }

    if(c->x >= 0){
	if(c->x < vs->width)
	    vs->width -= c->x;
	else
	    c->x = 0;
    } else {
	if(-c->x < vs->width){
	    int x = vs->width + c->x;
	    vs->width = -c->x;
	    c->x = x;
	} else {
	    c->x = 0;
	}
    }

    if(c->y >= 0){
	if(c->y < vs->height)
	    vs->height -= c->y;
	else
	    c->y = 0;
    } else {
	if(-c->y < vs->height){
	    int y = vs->height + c->y;
	    vs->height = -c->y;
	    c->y = y;
	} else {
	    c->y = 0;
	}
    }

    if(c->w > 0){
	if(c->w <= vs->width)
	    vs->width = c->w;
	else
	    c->w = vs->width;
    } else {
	if(-c->w < vs->width)
	    vs->width += c->w;
	c->w = vs->width;
    }

    if(c->h > 0){
	if(c->h <= vs->height)
	    vs->height = c->h;
	else
	    c->h = vs->height;
    } else {
	if(-c->h < vs->height)
	    vs->height += c->h;
	c->h = vs->height;
    }

    if(!(vs->width && vs->height))
	return PROBE_FAIL;

    vs->aspect.num = vs->width * sar.num;
    vs->aspect.den = vs->height * sar.den;
    tcreduce(&vs->aspect);

    return PROBE_OK;
}

extern int
crop_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    crop_t *c;

    c = tcallocz(sizeof(*c));
    tcconf_getvalue(cs, "width", "%i", &c->w);
    tcconf_getvalue(cs, "height", "%i", &c->h);
    tcconf_getvalue(cs, "x", "%i", &c->x);
    tcconf_getvalue(cs, "y", "%i", &c->y);

    p->private = c;

    return 0;
}
