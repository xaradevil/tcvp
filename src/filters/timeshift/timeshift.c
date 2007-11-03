/**
    Copyright (C) 2007  Måns Rullgård

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
#include <tcalloc.h>
#include <tcvp_types.h>
#include <timeshift_tc2.h>

struct timeshift {
    int64_t offset;
};

extern int
ts_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    struct timeshift *ts = p->private;

    if (pk->flags & TCVP_PKT_FLAG_PTS)
        pk->pts += ts->offset;
    if (pk->flags & TCVP_PKT_FLAG_DTS)
        pk->dts += ts->offset;

    return p->next->input(p->next, (tcvp_packet_t *) pk);
}

extern int
ts_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
       muxed_stream_t *ms)
{
    struct timeshift *ts = tcallocz(sizeof(*ts));

    if (!ts)
        return -1;

    tcconf_getvalue(cs, "offset", "%li", &ts->offset);
    ts->offset *= 27000;

    p->private = ts;

    return 0;
}
