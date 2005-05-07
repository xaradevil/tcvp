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
#include "ogg.h"

typedef struct theora_params {
    int gpshift;
    int gpmask;
} theora_params_t;

static int
theora_header(muxed_stream_t *ms, int idx)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os = ogg->streams + idx;
    stream_t *st = ms->streams + idx;
    theora_params_t *thp = os->private;
    int cds = st->common.codec_data_size + os->psize + 2;
    u_char *cdp;

    if(!(os->buf[os->pstart] & 0x80))
	return 0;

    if(!thp){
	thp = tcallocz(sizeof(*thp));
	os->private = thp;
    }

    st->common.codec_data = realloc(st->common.codec_data, cds);
    cdp = st->common.codec_data + st->common.codec_data_size;
    *cdp++ = os->psize >> 8;
    *cdp++ = os->psize & 0xff;
    memcpy(cdp, os->buf + os->pstart, os->psize);
    st->common.codec_data_size = cds;

    if(os->buf[os->pstart] == 0x80){
	u_char *p = os->buf + os->pstart + 7;
	p += 3;			/* version */
	st->video.width = htob_16(unaligned16(p)) * 16;
	p += 2;
	st->video.height = htob_16(unaligned16(p)) * 16;
	p += 2;
	p += 8;			/* frame cropping */
	st->video.frame_rate.num = htob_32(unaligned32(p));
	p += 4;
	st->video.frame_rate.den = htob_32(unaligned32(p));
	p += 4;
	p += 10;
	thp->gpshift = ((p[0] & 3) << 3) + ((p[1] & 0xe0) >> 5);
	thp->gpmask = (1 << thp->gpshift) - 1;

	st->stream_type = STREAM_TYPE_VIDEO;
	st->audio.codec = "video/theora";
    } else if(os->buf[os->pstart] == 0x81){
	vorbis_comment(ms, os->buf + os->pstart + 7, os->psize - 7);
    }

    return os->buf[os->pstart] & 0x80;
}

extern uint64_t
theora_gptopts(muxed_stream_t *ms, int idx, uint64_t gp)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os = ogg->streams + idx;
    theora_params_t *thp = os->private;
    stream_t *st = ms->streams + idx;

    uint64_t iframe = gp >> thp->gpshift;
    uint64_t pframe = gp & thp->gpmask;

    return (iframe + pframe) * st->video.frame_rate.den * 27000000LL /
	st->video.frame_rate.num;
}

ogg_codec_t theora_codec = {
    .magic = "\200theora",
    .magicsize = 7,
    .header = theora_header,
    .gptopts = theora_gptopts,
};
