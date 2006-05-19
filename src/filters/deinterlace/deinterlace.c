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
#include <ffmpeg/avcodec.h>
#include <tcvp_types.h>
#include <deinterlace_tc2.h>

#define DI_NONE  0
#define DI_DROP  1
#define DI_BLEND 2
#define DI_SPLIT 3

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
di_split(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    u_char *data[pk->planes], **od;
    int sizes[pk->planes], *os;
    int i;

    od = pk->data;
    os = pk->sizes;

    pk->data = data;
    pk->sizes = sizes;

    for(i = 0; i < pk->planes; i++){
	sizes[i] = os[i] * 2;
	data[i] = od[i];
    }

    p->next->input(p->next, tcref(pk));

    for(i = 0; i < pk->planes; i++)
	data[i] += os[i];

    pk->pts += 27000000LL * p->format.video.frame_rate.den /
	p->format.video.frame_rate.num;

    p->next->input(p->next, tcref(pk));

    pk->data = od;
    pk->sizes = os;

    tcfree(pk);
    return 0;
}

static int
di_blend(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    AVPicture pic;
    int i;

    for(i = 0; i < 3; i++){
	pic.data[i] = pk->data[i];
	pic.linesize[i] = pk->sizes[i];
    }

    avpicture_deinterlace(&pic, &pic, PIX_FMT_YUV420P, p->format.video.width,
			  p->format.video.height);

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
	case DI_SPLIT:
	    return di_split(p, pk);
	}
    } else {
	return p->next->input(p->next, (tcvp_packet_t *) pk);
    }

    return -1;
}

extern int
di_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    deinterlace_t *di = p->private;

    switch(di->method){
    case DI_SPLIT:
	p->format.video.frame_rate.num *= 2;
    case DI_DROP:
	p->format.video.height /= 2;
	break;
    }
    if(di->method)
	p->format.video.flags &= ~TCVP_STREAM_FLAG_INTERLACED;

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

    fprintf(stderr, "%s\n", dm);

    if(!dm || !strcmp(dm, "drop")){
	di->method = DI_DROP;
    } else if(!strcmp(dm, "none")){
	di->method = DI_NONE;
    } else if(!strcmp(dm, "blend")){
	di->method = DI_BLEND;
    } else if(!strcmp(dm, "split")){
	di->method = DI_SPLIT;
    } else {
	tc2_print("DEINTERLACE", TC2_PRINT_WARNING,
		  "unknown method '%s'\n", dm);
    }

    p->private = di;

    return 0;
}
