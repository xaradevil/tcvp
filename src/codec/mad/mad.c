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
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <tcvp_types.h>
#include <mad.h>
#include <mad_tc2.h>

#define MAD_BUFFER_SIZE (3*8065)

typedef struct mad_packet {
    packet_t pk;
    int16_t *data;
    int16_t *dp;
    int size;
} mad_packet_t;

typedef struct mad_dec {
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;
    pthread_mutex_t lock;
    int flush;
    packet_t *in;
    mad_packet_t *out;
    int bs, pfs;
    u_char buf[MAD_BUFFER_SIZE];
} mad_dec_t;

typedef struct mp3_frame {
    int version;
    int layer;
    int bitrate;
    int sample_rate;
    int size;
    int channels;
} mp3_frame_t;

static int bitrates[16][4] = {
    {  0,   0,   0,   0},
    { 32,  32,  32,   8},
    { 64,  48,  40,  16},
    { 96,  56,  48,  24},
    {128,  64,  56,  32},
    {160,  80,  64,  40},
    {192,  96,  80,  48},
    {224, 112,  96,  56},
    {256, 128, 112,  64},
    {288, 160, 128,  80},
    {320, 192, 160,  96},
    {352, 224, 192, 112},
    {384, 256, 224, 128},
    {416, 320, 256, 144},
    {448, 384, 320, 160},
    {  0,   0,   0,   0}
};

static int sample_rates[3][4] = {
    {11025, 0, 22050, 44100},
    {12000, 0, 24000, 48000},
    { 8000, 0, 16000, 32000}
};

static int
mp3_header(u_char *buf, mp3_frame_t *mf)
{
    int bx, br, sr, pad;
    int c = buf[1], d = buf[2], e = buf[3];

    if((c & 0xe0) != 0xe0 ||
       ((c & 0x18) == 0x08 ||
	(c & 0x06) == 0)){
	return -1;
    }
    if((d & 0xf0) == 0xf0 ||
       (d & 0x0c) == 0x0c){
	return -1;
    }

    if(!mf)
	return 0;

    mf->version = (c >> 3) & 0x3;
    mf->layer = 3 - ((c >> 1) & 0x3);
    bx = mf->layer + (mf->layer == 2? ~mf->version & 1: 0);
    br = (d >> 4) & 0xf;
    if(!bitrates[br][bx])
	return -1;

    sr = (d >> 2) & 3;
    pad = (d >> 1) & 1;
    mf->bitrate = bitrates[br][bx] * 1000;
    mf->sample_rate = sample_rates[sr][mf->version];
    mf->size = 144 * mf->bitrate / mf->sample_rate + pad;
    mf->channels = ((e >> 6) & 3) > 2? 1: 2;

    return 0;
}

static inline int
scale(mad_fixed_t sample)
{
    sample += (1L << (MAD_F_FRACBITS - 16));

    if (sample >= MAD_F_ONE)
	sample = MAD_F_ONE - 1;
    else if (sample < -MAD_F_ONE)
	sample = -MAD_F_ONE;

    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static void
mad_free_pk(packet_t *pk)
{
    mad_packet_t *mp = (mad_packet_t *) pk;
    free(mp->data);
    free(mp);
}

#define OUT_PACKET_SIZE(c) (24000 * (c))

static mad_packet_t *
mad_alloc(int c)
{
    mad_packet_t *mp;

    mp = calloc(1, sizeof(*mp));
    mp->data = malloc(OUT_PACKET_SIZE(c) * 2);
    mp->dp = mp->data;
    mp->pk.data = (u_char **) &mp->data;
    mp->pk.sizes = &mp->size;
    mp->pk.planes = 1;
    mp->pk.free = mad_free_pk;

    return mp;
}

static int
output(tcvp_pipe_t *tp, struct mad_header const *header, struct mad_pcm *pcm)
{
    mad_dec_t *md = tp->private;
    unsigned int channels, samples;
    const mad_fixed_t *left, *right;

    channels = pcm->channels;
    samples = pcm->length;
    left = pcm->samples[0];
    right = pcm->samples[1];

    if(!md->out)
	md->out = mad_alloc(channels);

    while(samples--){
	int sample;

	sample = scale(*left++);
	*md->out->dp++ = sample;

	if(channels == 2){
	    sample = scale(*right++);
	    *md->out->dp++ = sample;
	}

	if(md->out->dp -  md->out->data == OUT_PACKET_SIZE(channels)){
	    md->out->size = OUT_PACKET_SIZE(channels) * 2;
	    tp->next->input(tp->next, &md->out->pk);
	    md->out = mad_alloc(channels);
	}
    }

    return 0;
}

static int
do_decode(tcvp_pipe_t *p)
{
    mad_dec_t *md = p->private;

    mad_stream_buffer(&md->stream, md->buf, md->bs);

    while(!md->flush){
	if(mad_frame_decode(&md->frame, &md->stream) < 0){
	    if(MAD_RECOVERABLE(md->stream.error))
		continue;
	    else
		break;
	}
	mad_synth_frame(&md->synth, &md->frame);
	output(p, &md->frame.header, &md->synth.pcm);
    }

    return md->stream.error;
}

#define min(a,b) ((a)<(b)?(a):(b))

static int
decode(tcvp_pipe_t *p, packet_t *pk)
{
    mad_dec_t *md = p->private;
    u_char *d;
    int size;

    if(md->in){
	packet_t *in;
	pthread_mutex_lock(&md->lock);
	in = md->in;
	md->in = NULL;
	pthread_mutex_unlock(&md->lock);
	decode(p, in);
    }

    md->flush = 0;

    pthread_mutex_lock(&md->lock);

    if(!pk){
	if(md->bs)
	    do_decode(p);

	if(md->out && md->out->dp != md->out->data){
	    md->out->size = (md->out->dp - md->out->data) * 2;
	    p->next->input(p->next, &md->out->pk);
	    md->out = NULL;
	}
	p->next->input(p->next, NULL);
	goto out;
    }

    d = pk->data[0];
    size = pk->sizes[0];

    while(size > 0 && !md->flush){
	int bs = min(size, MAD_BUFFER_SIZE - md->bs);
	memcpy(md->buf + md->bs, d, bs);
	md->bs += bs;
	size -= bs;
	d += bs;

	if(md->bs == MAD_BUFFER_SIZE){
	    if(do_decode(p)){
		int rs = md->bs - (md->stream.this_frame - md->buf);
		if(rs == md->bs){
		    fprintf(stderr, "MAD: nothing decoded\n");
		    abort();
		}
		memmove(md->buf, md->stream.this_frame, rs);
		md->bs = rs;
	    } else {
		md->bs = 0;
	    }
	}
    }

out:
    pthread_mutex_unlock(&md->lock);
    if(pk)
	pk->free(pk);

    return 0;
}

static int
probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    mad_dec_t *md = p->private;
    mp3_frame_t mf;
    u_char *d = pk->data[0];

    if(mp3_header(d, &mf) < 0)
	return PROBE_FAIL;

    s->audio.sample_rate = mf.sample_rate;
    s->audio.channels = mf.channels;

    md->in = pk;

    return PROBE_OK;
}

static int
mad_flush(tcvp_pipe_t *p, int drop)
{
    mad_dec_t *md = p->private;

    if(drop){
	md->flush = 1;
	pthread_mutex_lock(&md->lock);
	if(md->in)
	    md->in->free(md->in);
	md->in = NULL;
	if(md->out)
	    mad_free_pk(&md->out->pk);
	md->out = NULL;
	md->bs = 0;
	md->pfs = 0;
	pthread_mutex_unlock(&md->lock);
    }

    return p->next->flush(p->next, drop);
}

static int
mad_free(tcvp_pipe_t *p)
{
    mad_dec_t *md = p->private;

    mad_synth_finish(&md->synth);
    mad_frame_finish(&md->frame);
    mad_stream_finish(&md->stream);

    free(md);
    free(p);

    return 0;
}

extern tcvp_pipe_t *
mad_new(stream_t *s, int mode)
{
    mad_dec_t *md;
    tcvp_pipe_t *p;

    if(mode != CODEC_MODE_DECODE)
	return NULL;

    md = calloc(1, sizeof(*md));
    mad_stream_init(&md->stream);
    mad_frame_init(&md->frame);
    mad_synth_init(&md->synth);

    p = calloc(1, sizeof(*p));
    p->input = decode;
    p->probe = probe;
    p->free = mad_free;
    p->flush = mad_flush;
    p->private = md;

    return p;
}
