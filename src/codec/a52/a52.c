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
#include <stdint.h>
#include <string.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <a52dec/a52.h>
#include <a52dec/mm_accel.h>
#include <a52_tc2.h>

typedef struct a52_decode {
    a52_state_t *state;
    sample_t *out;
    sample_t level, bias;
    int flags;
    char buf[3840];
    int fsize, fpos;
    uint64_t pts;
    int ptsf;
} a52_decode_t;

/* float to int convertion from a52dec by Aaron Holtzman and Michel
 * Lespinasse */
static inline int16_t convert (int32_t i)
{
    if (i > 0x43c07fff)
	return 32767;
    else if (i < 0x43bf8000)
	return -32768;
    else
	return i - 0x43c00000;
}

static inline int float_to_int (float * _f, int16_t * s16, int flags)
{
    int i;
    int32_t * f = (int32_t *) _f;

    switch (flags) {
    case A52_MONO:
	for (i = 0; i < 256; i++) {
	    s16[5*i] = s16[5*i+1] = s16[5*i+2] = s16[5*i+3] = 0;
	    s16[5*i+4] = convert (f[i]);
	}
	return 5;
    case A52_CHANNEL:
    case A52_STEREO:
    case A52_DOLBY:
	for (i = 0; i < 256; i++) {
	    s16[2*i] = convert (f[i]);
	    s16[2*i+1] = convert (f[i+256]);
	}
	return 2;
    case A52_3F:
	for (i = 0; i < 256; i++) {
	    s16[5*i] = convert (f[i]);
	    s16[5*i+1] = convert (f[i+512]);
	    s16[5*i+2] = s16[5*i+3] = 0;
	    s16[5*i+4] = convert (f[i+256]);
	}
	return 5;
    case A52_2F2R:
	for (i = 0; i < 256; i++) {
	    s16[4*i] = convert (f[i]);
	    s16[4*i+1] = convert (f[i+256]);
	    s16[4*i+2] = convert (f[i+512]);
	    s16[4*i+3] = convert (f[i+768]);
	}
	return 4;
    case A52_3F2R:
	for (i = 0; i < 256; i++) {
	    s16[5*i] = convert (f[i]);
	    s16[5*i+1] = convert (f[i+256]);
	    s16[5*i+2] = convert (f[i+512]);
	    s16[5*i+3] = convert (f[i+768]);
	    s16[5*i+4] = convert (f[i+1024]);
	}
	return 5;
    case A52_MONO | A52_LFE:
	for (i = 0; i < 256; i++) {
	    s16[6*i] = s16[6*i+1] = s16[6*i+2] = s16[6*i+3] = 0;
	    s16[6*i+4] = convert (f[i+256]);
	    s16[6*i+5] = convert (f[i]);
	}
	return 6;
    case A52_CHANNEL | A52_LFE:
    case A52_STEREO | A52_LFE:
    case A52_DOLBY | A52_LFE:
	for (i = 0; i < 256; i++) {
	    s16[6*i] = convert (f[i+256]);
	    s16[6*i+1] = convert (f[i+512]);
	    s16[6*i+2] = s16[6*i+3] = s16[6*i+4] = 0;
	    s16[6*i+5] = convert (f[i]);
	}
	return 6;
    case A52_3F | A52_LFE:
	for (i = 0; i < 256; i++) {
	    s16[6*i] = convert (f[i+256]);
	    s16[6*i+1] = convert (f[i+768]);
	    s16[6*i+2] = s16[6*i+3] = 0;
	    s16[6*i+4] = convert (f[i+512]);
	    s16[6*i+5] = convert (f[i]);
	}
	return 6;
    case A52_2F2R | A52_LFE:
	for (i = 0; i < 256; i++) {
	    s16[6*i] = convert (f[i+256]);
	    s16[6*i+1] = convert (f[i+512]);
	    s16[6*i+2] = convert (f[i+768]);
	    s16[6*i+3] = convert (f[i+1024]);
	    s16[6*i+4] = 0;
	    s16[6*i+5] = convert (f[i]);
	}
	return 6;
    case A52_3F2R | A52_LFE:
	for (i = 0; i < 256; i++) {
	    s16[6*i] = convert (f[i+256]);
	    s16[6*i+1] = convert (f[i+768]);
	    s16[6*i+2] = convert (f[i+1024]);
	    s16[6*i+3] = convert (f[i+1280]);
	    s16[6*i+4] = convert (f[i+512]);
	    s16[6*i+5] = convert (f[i]);
	}
	return 6;
    default:
	tc2_print("A52", TC2_PRINT_ERROR, "invalid flags\n");
    }
    return 0;
}

static void
a52_free_pk(void *v)
{
    packet_t *p = v;
    free(p->sizes);
    free(p->private);
}

extern int
a52_decode(tcvp_pipe_t *p, packet_t *pk)
{
    a52_decode_t *ad = p->private;
    int psize;
    char *pdata;
    int i;
    int ret = 0;

    if(!pk->data){
	p->next->input(p->next, pk);
	return 0;
    }

    psize = pk->sizes[0];
    pdata = pk->data[0];

    if(pk->flags & TCVP_PKT_FLAG_PTS){
	ad->pts = pk->pts;
	ad->ptsf = 1;
    }

    while(psize > 0){
	if(ad->fsize > 0){
	    int fs = ad->fsize - ad->fpos;
	    if(fs > psize){
		memcpy(ad->buf + ad->fpos, pdata, psize);
		ad->fpos += psize;
		break;
	    } else {
		memcpy(ad->buf + ad->fpos, pdata, fs);
		psize -= fs;
		pdata += fs;
		ad->fpos += fs;
	    }
	} else {
	    int flags, srate, brate, size = 0, od;

	    for(od = 0; od < psize - 7; od++){
		if((size = a52_syncinfo(pdata + od, &flags, &srate, &brate)))
		    break;
	    }

	    if(!size){
		ret = -1;
		break;
	    }

	    psize -= od;
	    pdata += od;
	    ad->fsize = size;
	    continue;
	}

	a52_frame(ad->state, ad->buf, &ad->flags, &ad->level, ad->bias);
	ad->flags &= ~A52_ADJUST_LEVEL;

	for(i = 0; i < 6; i++){
	    int s;
	    packet_t *out;
	    int16_t *outbuf = malloc(6*256*sizeof(*outbuf));

	    a52_block(ad->state);
	    s = float_to_int(ad->out, outbuf, ad->flags);

	    out = tcallocd(sizeof(*out), NULL, a52_free_pk);
	    out->type = TCVP_PKT_TYPE_DATA;
	    out->stream = pk->stream;
	    out->data = (u_char **) &out->private;
	    out->sizes = malloc(sizeof(*out->sizes));
	    out->sizes[0] = 256 * s * sizeof(*outbuf);
	    out->planes = 1;
	    out->private = outbuf;
	    out->flags = 0;
	    if(ad->ptsf){
		out->flags |= TCVP_PKT_FLAG_PTS;
		out->pts = ad->pts;
		ad->ptsf = 0;
	    }

	    p->next->input(p->next, out);
	}

	ad->fpos = 0;
	ad->fsize = 0;
    }

    tcfree(pk);

    return ret;
}

extern int
a52_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    int flags, srate, bitrate, size = 0;
    int od;

    for(od = 0; od < pk->sizes[0] - 7; od++){
	size = a52_syncinfo(pk->data[0] + od, &flags, &srate, &bitrate);
	if(size)
	    break;
    }

    if(!size)
	return PROBE_AGAIN;

    s->common.bit_rate = bitrate;

    p->format = *s;
    p->format.stream_type = STREAM_TYPE_AUDIO;
    p->format.common.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->format.audio.sample_rate = srate;
    p->format.audio.channels = 2;
    p->format.audio.bit_rate = srate * 32;

    tcfree(pk);

    return PROBE_OK;
}

static void
a52_free_codec(void *p)
{
    a52_decode_t *ad = p;

    a52_free(ad->state);
}

extern int
a52_flush(tcvp_pipe_t *p, int drop)
{
    a52_decode_t *ad = p->private;

    if(drop){
	ad->fsize = 0;
	ad->fpos = 0;
    }

    return 0;
}

extern int
a52_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
	tcvp_timer_t *t, muxed_stream_t *ms)
{
    a52_decode_t *ad;

    ad = tcallocdz(sizeof(*ad), NULL, a52_free_codec);
    ad->state = a52_init(0);
    ad->out = a52_samples(ad->state);
    ad->level = 1;
    ad->bias = 384;
    ad->flags = A52_STEREO | A52_ADJUST_LEVEL;

    p->private = ad;

    return 0;
}
