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
#include <string.h>
#include <tcalloc.h>
#include <pcmfmt_tc2.h>
#include <pcmmod.h>

static void
pcmw_free(void *p)
{
    pcm_write_t *pcm = p;
    if(pcm->close)
	pcm->close(pcm);
    tcfree(pcm->u);
}

extern int
pcmw_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    pcm_write_t *pcm;
    char *file;
    url_t *u;

    if(tcconf_getvalue(cs, "mux/url", "%s", &file) <= 0)
	return -1;

    if(!(u = url_open(file, "w")))
	return -1;

    pcm = tcallocdz(sizeof(*pcm), NULL, pcmw_free);
    pcm->u = u;
    p->private = pcm;

    return 0;
}

extern int
pcmw_packet(tcvp_pipe_t *p, packet_t *pk)
{
    pcm_write_t *pcm = p->private;

    if(!pk->data)
	return 0;

    pcm->u->write(pk->data[0], 1, pk->sizes[0], pcm->u);

    return 0;
}
