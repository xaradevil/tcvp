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
#include <stdio.h>
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <deinterlace_tc2.h>

#define DI_NONE  0
#define DI_DROP  1
#define DI_BLEND 2

typedef struct deinterlace {
    int method;
} deinterlace_t;

static int
di_drop(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    int i;

    for(i = 0; i < pk->planes; i++)
	pk->sizes[i] *= 2;

    return p->next->input(p->next, (tcvp_packet_t *) pk);
}

static int
di_blend(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    int i, j, k;

    for(i = 0; i < 3; i++){
	int d = i? 2: 1;
	for(j = 0; j < p->format.video.height / d - 1; j += 2){
	    for(k = 0; k < p->format.video.width / d; k++){
		pk->data[i][j * pk->sizes[i] + k] =
		    (pk->data[i][j * pk->sizes[i] + k] +
		     pk->data[i][(j+1) * pk->sizes[i] + k]) / 2;
	    }
	}
    }

    return p->next->input(p->next, (tcvp_packet_t *) pk);
}

extern int
di_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    deinterlace_t *di = p->private;

    if(pk->data){
	switch(di->method){
	case DI_NONE:
	    return p->next->input(p->next, (tcvp_packet_t *) pk);
	case DI_DROP:
	    return di_drop(p, pk);
	case DI_BLEND:
	    return di_blend(p, pk);
	}
    }

    return -1;
}

extern int
di_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    deinterlace_t *di = p->private;

    if(p->format.video.flags & TCVP_STREAM_FLAG_INTERLACED){
	switch(di->method){
	case DI_DROP:
	    p->format.video.height /= 2;
	    break;
	}
	if(di->method)
	    p->format.video.flags &= ~TCVP_STREAM_FLAG_INTERLACED;
    }

    return PROBE_OK;
}

extern int
di_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
       muxed_stream_t *ms)
{
    deinterlace_t *di;
    char *dm = NULL;

    tcconf_getvalue(cs, "method", "%s", &dm);
    di = tcallocz(sizeof(*di));

    if(!dm || !strcmp(dm, "drop")){
	di->method = DI_DROP;
    } else if(!strcmp(dm, "none")){
	di->method = DI_NONE;
    } else if(!strcmp(dm, "blend")){
	di->method = DI_BLEND;
    } else {
	tc2_print("DEINTERLACE", TC2_PRINT_WARNING,
		  "unknown method '%s'\n", dm);
    }

    p->private = di;

    return 0;
}
