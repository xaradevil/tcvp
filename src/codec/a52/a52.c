/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
    packet_t *in;
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
    }
    return 0;
}

static void
a52_free_pk(packet_t *p)
{
    free(p->sizes);
    free(p->private);
    free(p);
}

static packet_t *
do_decode(tcvp_pipe_t *p, packet_t *pk)
{
    a52_decode_t *ad = p->private;
    int psize;
    char *pdata;
    int i, s;
    packet_t *out = NULL;
    int16_t *outbuf;

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
	    int flags, size = 0, od;

	    for(od = 0; od < psize - 7; od++){
		if((size = a52_syncinfo(pdata + od, &flags,
					&p->format.audio.sample_rate,
					&p->format.audio.bit_rate)))
		    break;
	    }

	    if(!size)
		break;

	    psize -= od;
	    pdata += od;
	    ad->fsize = size;
	    continue;
	}

	a52_frame(ad->state, ad->buf, &ad->flags, &ad->level, ad->bias);
	ad->flags &= ~A52_ADJUST_LEVEL;

	outbuf = malloc(6*6*256*sizeof(*outbuf));
	out = malloc(sizeof(*out));
	out->data = (u_char **) &out->private;
	out->sizes = malloc(sizeof(*out->sizes));
	out->planes = 1;
	out->free = a52_free_pk;
	out->private = outbuf;
	out->flags = 0;
	if(ad->ptsf){
	    out->flags |= TCVP_PKT_FLAG_PTS;
	    out->pts = ad->pts;
	    ad->ptsf = 0;
	}

	for(i = 0; i < 6; i++){
	    a52_block(ad->state);
	    s = float_to_int(ad->out, outbuf, ad->flags);
	    outbuf += 256 * s;
	}

	out->sizes[0] = 6*256 * s * sizeof(*outbuf);

	ad->fpos = 0;
	ad->fsize = 0;
    }

    pk->free(pk);

    return out;
}


static int
a52_decode(tcvp_pipe_t *p, packet_t *pk)
{
    packet_t *dp;

    if(!pk){
	p->next->input(p->next, NULL);
	return 0;
    }

    if((dp = do_decode(p, pk)))
	p->next->input(p->next, dp);

    return 0;
}

static int
a52_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    packet_t *out;
    int ret = PROBE_AGAIN;

    p->format = *s;
    p->format.audio.channels = 2;

    if((out = do_decode(p, pk)))
	ret = p->next->probe(p->next, out, &p->format);

    return ret;
}

static void
a52_free_codec(void *p)
{
    tcvp_pipe_t *tp = p;
    a52_decode_t *ad = tp->private;
    a52_free(ad->state);
    free(ad);
}

static int
a52_flush(tcvp_pipe_t *p, int drop)
{
    a52_decode_t *ad = p->private;

    if(drop && ad->in){
	ad->in->free(ad->in);
	ad->in = NULL;
    }

    return p->next->flush(p->next, drop);
}

extern tcvp_pipe_t *
a52_new(stream_t *s, conf_section *cs, timer__t *t)
{
    a52_decode_t *ad;
    tcvp_pipe_t *p;

    ad = calloc(1, sizeof(*ad));
    ad->state = a52_init(0);
    ad->out = a52_samples(ad->state);
    ad->level = 1;
    ad->bias = 384;
    ad->flags = A52_STEREO | A52_ADJUST_LEVEL;

    p = tcallocdz(sizeof(*p), NULL, a52_free_codec);
    p->input = a52_decode;
    p->probe = a52_probe;
    p->flush = a52_flush;
    p->private = ad;

    return p;
}
