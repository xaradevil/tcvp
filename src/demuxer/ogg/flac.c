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

static int
flac_header(muxed_stream_t *ms, int idx)
{
    ogg_t *ogg = ms->private;
    ogg_stream_t *os = ogg->streams + idx;
    stream_t *st = ms->streams + idx;
    u_char *p = os->buf + os->pstart;
    int size = os->psize;

    if(*p == 0xff)
        return 0;

    if((*p & 0x7f) == 0x7f){
        p += 17;
        size -= 17;
        st->stream_type = STREAM_TYPE_AUDIO;
        st->audio.codec = "audio/flac";
        audio_x_flac_streaminfo(ms, st, p, size);
        st->audio.codec_data = malloc(size);
        st->audio.codec_data_size = size;
        memcpy(st->audio.codec_data, p, size);
    } else if((*p & 0x7f) == 4){
        p += 4;
        size -= 4;
        vorbis_comment(ms, p, size);
    }

    return 1;
}

ogg_codec_t flac_codec = {
    .magic = "\177FLAC",
    .magicsize = 5,
    .header = flac_header
};
