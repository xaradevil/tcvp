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
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <ffmpeg/avcodec.h>
#include <avcodec_tc2.h>
#include "avc.h"

static int
do_decaudio(tcvp_pipe_t *p, packet_t *pk, int probe)
{
    packet_t *out;
    avc_codec_t *ac = p->private;
    char *inbuf;
    int insize;

    if(pk->data){
	inbuf = pk->data[0];
	insize = pk->sizes[0];
    } else {
	p->next->input(p->next, pk);
	return 0;
    }

    while(insize > 0){
	int l, outsize;

	l = avcodec_decode_audio(ac->ctx, (int16_t *) ac->buf, &outsize,
				 inbuf, insize);
	if(l < 0){
	    return -1;
	}

	inbuf += l;
	insize -= l;

	if(outsize > 0){
	    if(probe){
		ac->have_params = 1;
		break;
	    }

	    out = tcallocd(sizeof(*out), NULL, avc_free_packet);
	    out->type = TCVP_PKT_TYPE_DATA;
	    out->stream = pk->stream;
	    out->data = (u_char **) &out->private;
	    out->sizes = malloc(sizeof(*out->sizes));
	    out->sizes[0] = outsize;
	    out->planes = 1;
	    out->pts = pk->pts;
	    pk->pts = 0;
	    out->private = ac->buf;
	    p->next->input(p->next, out);
	}
    }

    tcfree(pk);

    return 0;
}

extern int
avc_decaudio(tcvp_pipe_t *p, packet_t *pk)
{
    return do_decaudio(p, pk, 0);
}

extern int
avc_probe_audio(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    avc_codec_t *ac = p->private;
    int ret;

    if(do_decaudio(p, pk, 1) < 0)
	return PROBE_FAIL;

    if(ac->have_params){
	p->format = *s;
	p->format.audio.codec = "audio/pcm-s16" TCVP_ENDIAN;
	p->format.audio.sample_rate = ac->ctx->sample_rate;
	p->format.audio.channels = ac->ctx->channels;
	ret = PROBE_OK;
    } else {
	ret = PROBE_AGAIN;
    }

    return ret;
}