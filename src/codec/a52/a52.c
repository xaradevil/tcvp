/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

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
#include <tcvp_types.h>
#include <a52dec/a52.h>
#include <a52dec/mm_accel.h>
#include <a52_tc2.h>

typedef struct a52_decode {
    a52_state_t state;
    sample_t *out;
    packet_t *in;
    sample_t level, bias;
    int flags;
    char buf[3840];
    int fsize, fpos;
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
}

static void
a52_free_pk(packet_t *p)
{
    free(p->sizes);
    free(p->private);
    free(p);
}

static int
a52_decode(tcvp_pipe_t *p, packet_t *pk)
{
    a52_decode_t *ad = p->private;
    int psize = pk->sizes[0];
    char *pdata = pk->data[0];
    int i;
    int ret = 0;

    if(ad->in){
	packet_t *in = ad->in;
	ad->in = NULL;
	a52_decode(p, in);
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
	    int flags, srate, brate, size, od;

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

	a52_frame(&ad->state, ad->buf, &ad->flags, &ad->level, ad->bias);
	ad->flags &= ~A52_ADJUST_LEVEL;

	for(i = 0; i < 6; i++){
	    int s;
	    packet_t *out;
	    int16_t *outbuf = malloc(6*256*sizeof(*outbuf));

	    a52_block(&ad->state, ad->out);
	    s = float_to_int(ad->out, outbuf, ad->flags);

	    out = malloc(sizeof(*out));
	    out->data = (u_char **) &out->private;
	    out->sizes = malloc(sizeof(*out->sizes));
	    out->sizes[0] = 256 * s * sizeof(*outbuf);
	    out->planes = 1;
	    out->free = a52_free_pk;
	    out->private = outbuf;

	    p->next->input(p->next, out);
	}

	ad->fpos = 0;
	ad->fsize = 0;
    }

    pk->free(pk);

    return ret;
}

static int
a52_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    a52_decode_t *ad = p->private;
    int flags, srate, bitrate, size;
    int od;

    for(od = 0; od < pk->sizes[0] - 7; od++){
	size = a52_syncinfo(pk->data[0] + od, &flags, &srate, &bitrate);
	if(size)
	    break;
    }

    if(!size)
	return PROBE_FAIL;

    s->stream_type = STREAM_TYPE_AUDIO;
    s->audio.sample_rate = srate;
    s->audio.channels = 2;

    ad->in = pk;

    return PROBE_OK;
}

static int
a52_free(tcvp_pipe_t *p)
{
    free(p->private);
    free(p);
}

static int
a52_flush(tcvp_pipe_t *p, int drop)
{
    p->next->flush(p->next, drop);
}

extern tcvp_pipe_t *
a52_new(stream_t *s, int mode)
{
    a52_decode_t *ad;
    tcvp_pipe_t *p;

    if(mode != CODEC_MODE_DECODE)
	return NULL;

    ad = calloc(1, sizeof(*ad));
    ad->out = a52_init(0);
    ad->level = 1;
    ad->bias = 384;
    ad->flags = A52_STEREO | A52_ADJUST_LEVEL;

    p = calloc(1, sizeof(*p));
    p->input = a52_decode;
    p->free = a52_free;
    p->probe = a52_probe;
    p->flush = a52_flush;
    p->private = ad;

    return p;
}