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
#include <tcalloc.h>
#include <tcendian.h>
#include <tcmath.h>
#include "ogg.h"

static int
ogm_header(muxed_stream_t *ms, int idx)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os = ogg->streams + idx;
    stream_t *st = ms->streams + idx;
    u_char *p = os->buf + os->pstart;
    uint64_t time_unit;
    uint64_t spu;
    uint32_t default_len;

    if(!(*p & 1))
	return 0;
    if(*p != 1)
	return 1;

    p++;

    if(*p == 'v'){
	st->stream_type = STREAM_TYPE_VIDEO;
	p += 8;
	st->video.codec = video_x_msvideo_vcodec(p);
    } else {
	int cid;
	st->stream_type = STREAM_TYPE_AUDIO;
	p += 8;
	p[4] = 0;
	cid = strtol(p, NULL, 16);
	st->audio.codec = video_x_msvideo_acodec(cid);
    }

    p += 4;
    p += 4;			/* useless size field */

    time_unit = htol_64(unaligned64(p));
    p += 8;
    spu = htol_64(unaligned64(p));
    p += 8;
    default_len = htol_32(unaligned32(p));
    p += 4;

    p += 8;			/* buffersize + bits_per_sample */

    if(st->stream_type == STREAM_TYPE_VIDEO){
	st->video.width = htol_32(unaligned32(p));
	p += 4;
	st->video.height = htol_32(unaligned32(p));
	st->video.frame_rate.num = spu * 10000000;
	st->video.frame_rate.den = time_unit;
	tcreduce(&st->video.frame_rate);
    } else {
	st->audio.channels = htol_16(unaligned16(p));
	p += 2;
	p += 2;			/* block_align */
	st->audio.bit_rate = htol_32(unaligned32(p)) * 8;
	st->audio.sample_rate = spu * 10000000 / time_unit;
    }

    return 1;
}

static int
ogm_dshow_header(muxed_stream_t *ms, int idx)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os = ogg->streams + idx;
    stream_t *st = ms->streams + idx;
    u_char *p = os->buf + os->pstart;
    uint32_t t;

    if(!(*p & 1))
	return 0;
    if(*p != 1)
	return 1;

    t = htol_32(unaligned32(p + 96));

    if(t == 0x05589f80){
	st->stream_type = STREAM_TYPE_VIDEO;
	st->video.codec = video_x_msvideo_vcodec(p + 68);
	st->video.frame_rate.num = 10000000;
	st->video.frame_rate.den = htol_64(unaligned64(p + 164));
	st->video.width = htol_32(unaligned32(p + 176));
	st->video.height = htol_32(unaligned32(p + 180));
    } else if(t ==0x05589f81){
	st->stream_type = STREAM_TYPE_AUDIO;
	st->audio.codec = video_x_msvideo_acodec(htol_16(unaligned16(p+124)));
	st->audio.channels = htol_16(unaligned16(p + 126));
	st->audio.sample_rate = htol_32(unaligned32(p + 128));
	st->audio.bit_rate = htol_32(unaligned32(p + 132)) * 8;
    }

    return 1;
}

static int
ogm_packet(muxed_stream_t *ms, int idx)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os = ogg->streams + idx;
    u_char *p = os->buf + os->pstart;
    int lb;

    lb = ((*p & 2) << 1) | ((*p >> 6) & 3);
    os->pstart += lb + 1;
    os->psize -= lb + 1;

    return 0;
}

ogg_codec_t ogm_video_codec = {
    .magic = "\001video",
    .magicsize = 6,
    .header = ogm_header,
    .packet = ogm_packet
};

ogg_codec_t ogm_audio_codec = {
    .magic = "\001audio",
    .magicsize = 6,
    .header = ogm_header,
    .packet = ogm_packet
};

ogg_codec_t ogm_old_codec = {
    .magic = "\001Direct Show Samples embedded in Ogg",
    .magicsize = 35,
    .header = ogm_dshow_header,
    .packet = ogm_packet
};
