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
#include <tcmath.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <crop_tc2.h>

typedef struct crop {
    int x, y, w, h;
} crop_t;

static int
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

static int
crop_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    crop_t *c = p->private;
    video_stream_t *vs = &p->format.video;
    double pa = 1.0;

    if(vs->aspect.num)
	pa = (double) vs->height * vs->aspect.num / vs->aspect.den / vs->width;

    p->format = *s;

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

    vs->aspect.num = vs->width * pa;
    vs->aspect.den = vs->height;
    tcreduce(&vs->aspect);

    return p->next->probe(p->next, pk, &p->format);
}

static int
crop_flush(tcvp_pipe_t *p, int drop)
{
    return p->next->flush(p->next, drop);
}

static void
crop_free(void *p)
{
    tcvp_pipe_t *tp = p;
    free(tp->private);
}

extern tcvp_pipe_t *
crop_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t **t)
{
    tcvp_pipe_t *p;
    crop_t *c;

    c = calloc(1, sizeof(*c));
    tcconf_getvalue(cs, "width", "%i", &c->w);
    tcconf_getvalue(cs, "height", "%i", &c->h);
    tcconf_getvalue(cs, "x", "%i", &c->x);
    tcconf_getvalue(cs, "y", "%i", &c->y);

    p = tcallocdz(sizeof(*p), NULL, crop_free);
    p->format = *s;
    p->input = crop_input;
    p->probe = crop_probe;
    p->flush = crop_flush;
    p->private = c;

    return p;
}
