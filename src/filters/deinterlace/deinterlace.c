/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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

extern int
di_input(tcvp_pipe_t *p, packet_t *pk)
{
    int i;

    if(pk->data && p->format.video.flags & TCVP_STREAM_FLAG_INTERLACED){
	for(i = 0; i < pk->planes; i++)
	    pk->sizes[i] *= 2;
    }

    p->next->input(p->next, pk);

    return 0;
}

extern int
di_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    if(p->format.video.flags & TCVP_STREAM_FLAG_INTERLACED){
	p->format.video.height /= 2;
	p->format.video.flags &= ~TCVP_STREAM_FLAG_INTERLACED;
    }

    return 0;
}
