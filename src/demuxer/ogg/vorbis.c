/**
    Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

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
#include <tcstring.h>
#include <tctypes.h>
#include <tcendian.h>
#include <tcalloc.h>
#include <ctype.h>
#include "ogg.h"

extern int
vorbis_comment(muxed_stream_t *ms, char *buf, int size)
{
    char *p = buf;
    int s, n, j;

    if(size < 4)
	return -1;

    s = htol_32(unaligned32(p));
    p += 4;
    size -= 4;

    if(size < s + 4)
	return -1;

    p += s;
    size -= s;

    n = htol_32(unaligned32(p));
    p += 4;
    size -= 4;

    while(size >= 4){
	char *t, *v;
	int tl, vl;

	s = htol_32(unaligned32(p));
	p += 4;
	size -= 4;

	if(size < s)
	    break;

	t = p;
	p += s;
	size -= s;
	n--;

	v = memchr(t, '=', s);
	if(!v)
	    continue;

	tl = v - t;
	vl = s - tl - 1;
	v++;

	if(tl && vl){
	    char tt[tl + 1];
	    char *ct;

	    for(j = 0; j < tl; j++)
		tt[j] = tolower(t[j]);
	    tt[tl] = 0;

	    ct = malloc(vl + 1);
	    memcpy(ct, v, vl);
	    ct[vl] = 0;
	    tcattr_set(ms, tt, ct, NULL, free);
	}
    }

    if(size > 0)
	tc2_print("OGG", TC2_PRINT_WARNING,
		  "%i bytes of comment header remain\n", size);
    if(n > 0)
	tc2_print("OGG", TC2_PRINT_WARNING,
		  "truncated comment header, %i comments not found\n", n);

    return 0;
}

static int
vorbis_header(muxed_stream_t *ms, int idx)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os = ogg->streams + idx;
    stream_t *st = ms->streams + idx;
    int cds = st->common.codec_data_size + os->psize + 2;
    u_char *cdp;

    if(os->seq > 2)
	return 0;

    st->common.codec_data = realloc(st->common.codec_data, cds);
    cdp = st->common.codec_data + st->common.codec_data_size;
    *cdp++ = os->psize >> 8;
    *cdp++ = os->psize & 0xff;
    memcpy(cdp, os->buf + os->pstart, os->psize);
    st->common.codec_data_size = cds;

    if(os->buf[os->pstart] == 1){
	u_char *p = os->buf + os->pstart + 11;
	st->audio.channels = *p++;
	st->audio.sample_rate = htol_32(unaligned32(p));
	p += 8;
	st->audio.bit_rate = htol_32(unaligned32(p));
	ms->time = st->audio.samples * 27000000LL / st->audio.sample_rate;

	st->stream_type = STREAM_TYPE_AUDIO;
	st->audio.codec = "audio/vorbis";
    } else if(os->buf[os->pstart] == 3){
	vorbis_comment(ms, os->buf + os->pstart + 7, os->psize - 8);
    }

    return os->seq < 3;
}

ogg_codec_t vorbis_codec = {
    .magic = "\001vorbis",
    .magicsize = 7,
    .header = vorbis_header
};
