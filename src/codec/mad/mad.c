/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <mad.h>
#include <mad_tc2.h>

#define BUFFER_SIZE (tcvp_codec_mad_conf_input_buffer)

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

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
    mad_packet_t *out;
    int channels;
    int bs;
    int bufsize;
    u_char *buf;
    uint64_t npts;
    int ptsc;
    int pc;
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
mp3_header(u_char *buf, mp3_frame_t *mf, int size)
{
    int bx, br, sr, pad;
    int c = buf[1], d = buf[2], e = buf[3];

    if(size < 4)
	return -1;

    if(buf[0] != 0xff)
	return -1;

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
    if(!mf->sample_rate)
	return -1;

    mf->size = 144 * mf->bitrate / mf->sample_rate + pad;
    mf->channels = ((e >> 6) & 3) > 2? 1: 2;

/*     fprintf(stderr, "MP3: layer %i, version %i, rate %i, channels %i\n", */
/* 	    mf->layer, mf->version, mf->bitrate, mf->channels); */

    if(size > mf->size)
	return mp3_header(buf + mf->size, NULL, size - mf->size);
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
mad_free_pk(void *p)
{
    mad_packet_t *mp = (mad_packet_t *) p;
    free(mp->data);
}

#define OUT_PACKET_SIZE(c) (tcvp_codec_mad_conf_output_buffer * (c))

static mad_packet_t *
mad_alloc(int c, int s)
{
    mad_packet_t *mp;

    mp = tcallocdz(sizeof(*mp), NULL, mad_free_pk);
    mp->data = malloc(OUT_PACKET_SIZE(c) * 2);
    mp->dp = mp->data;
    mp->pk.stream = s;
    mp->pk.data = (u_char **) &mp->data;
    mp->pk.sizes = &mp->size;
    mp->pk.planes = 1;

    return mp;
}

static int
output(tcvp_pipe_t *tp, struct mad_pcm *pcm, int stream)
{
    mad_dec_t *md = tp->private;
    unsigned int channels, samples;
    const mad_fixed_t *left, *right;

    channels = pcm->channels;
    samples = pcm->length;
    left = pcm->samples[0];
    right = pcm->samples[1];

    if(!md->out)
	md->out = mad_alloc(md->channels, stream);

    while(samples--){
	int sample;

	sample = scale(*left++);
	*md->out->dp++ = sample;

	if(channels == 2){
	    sample = scale(*right++);
	    *md->out->dp++ = sample;
	}

	if(md->out->dp - md->out->data == OUT_PACKET_SIZE(md->channels)){
	    md->out->size = OUT_PACKET_SIZE(md->channels) * 2;
	    tp->next->input(tp->next, &md->out->pk);
	    md->out = mad_alloc(md->channels, stream);
	}
    }

    return 0;
}

static int
do_decode(tcvp_pipe_t *p, packet_t *pk)
{
    mad_dec_t *md = p->private;

    mad_stream_buffer(&md->stream, md->buf, md->bs);

    while(!md->flush){
	if(mad_frame_decode(&md->frame, &md->stream) < 0){
	    if(MAD_RECOVERABLE(md->stream.error)){
		continue;
	    } else {
		break;
	    }
	}
	mad_synth_frame(&md->synth, &md->frame);
	if(md->ptsc > 0){
	    md->ptsc -= md->stream.next_frame - md->stream.this_frame;
	    if(md->ptsc <= 0 && md->out){
		md->out->pk.pts = md->npts - ((md->out->dp - md->out->data) /
		    md->synth.pcm.channels) * 27000000 /
		    md->frame.header.samplerate;
		md->out->pk.flags |= TCVP_PKT_FLAG_PTS;
	    }
	}
	output(p, &md->synth.pcm, pk->stream);
    }

    return md->stream.error;
}

static int
decode(tcvp_pipe_t *p, packet_t *pk)
{
    mad_dec_t *md = p->private;
    u_char *d;
    int size;

    md->flush = 0;

    pthread_mutex_lock(&md->lock);

    if(!pk->data){
	if(md->bs)
	    do_decode(p, pk);

	if(md->out && md->out->dp != md->out->data){
	    md->out->size = (md->out->dp - md->out->data) * 2;
	    p->next->input(p->next, &md->out->pk);
	    md->out = NULL;
	}
	p->next->input(p->next, pk);
	pk = NULL;
	goto out;
    }

    d = pk->data[0];
    size = pk->sizes[0];

    if((pk->flags & TCVP_PKT_FLAG_PTS) && md->ptsc <= 0){
	md->npts = pk->pts;
	md->ptsc = md->bs;
    }

    while(size > 0 && !md->flush){
	int bs = min(size, md->bufsize - md->bs);
	int nd = 0;

	memcpy(md->buf + md->bs, d, bs);
	md->bs += bs;
	size -= bs;
	d += bs;

	if(md->bs == md->bufsize){
	    if(do_decode(p, pk)){
		int rs = md->bs - (md->stream.this_frame - md->buf);
		if(rs == md->bs){
		    md->buf = realloc(md->buf, md->bufsize *= 2);
		    if(++nd == 8){
			fprintf(stderr, "MAD: nothing decoded, bad file?\n");
			goto out;
		    }
		} else {
		    memmove(md->buf, md->stream.this_frame, rs);
		}
		md->bs = rs;
	    } else {
		md->bs = 0;
	    }
	}
    }

out:
    pthread_mutex_unlock(&md->lock);
    if(pk)
	tcfree(pk);

    return 0;
}

static int
probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    mad_dec_t *md = p->private;
    mp3_frame_t mf;
    u_char *d = pk->data[0];
    int ds = pk->sizes[0];

    while(mp3_header(d, &mf, ds)){
	d++;
	ds--;
	md->pc++;
    }

    if(ds <= 4){
	tcfree(pk);
	return md->pc > tcvp_codec_mad_conf_probe_max? PROBE_FAIL: PROBE_AGAIN;
    }

    p->format = *s;
    p->format.audio.codec = "audio/pcm-s16" TC2_ENDIAN;
    p->format.audio.sample_rate = mf.sample_rate;
    p->format.audio.channels = mf.channels;
    p->format.audio.bit_rate = mf.channels * mf.sample_rate * 16;

    s->audio.sample_rate = mf.sample_rate;
    s->audio.channels = mf.channels;
    s->audio.bit_rate = mf.bitrate;
    md->channels = mf.channels;

    tcfree(pk);

    return p->next->probe(p->next, NULL, &p->format);
}

static int
mad_flush(tcvp_pipe_t *p, int drop)
{
    mad_dec_t *md = p->private;

    if(drop){
	md->flush = 1;
	pthread_mutex_lock(&md->lock);
	if(md->out)
	    mad_free_pk(&md->out->pk);
	md->out = NULL;
	md->bs = 0;
	pthread_mutex_unlock(&md->lock);
    }

    return p->next->flush(p->next, drop);
}

static void
mad_free(void *p)
{
    tcvp_pipe_t *tp = p;
    mad_dec_t *md = tp->private;

    mad_synth_finish(&md->synth);
    mad_frame_finish(&md->frame);
    mad_stream_finish(&md->stream);
    free(md->buf);
    free(md);
}

extern tcvp_pipe_t *
mad_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    mad_dec_t *md;
    tcvp_pipe_t *p;

    md = calloc(1, sizeof(*md));
    mad_stream_init(&md->stream);
    mad_frame_init(&md->frame);
    mad_synth_init(&md->synth);
    md->bufsize = BUFFER_SIZE;
    md->buf = malloc(BUFFER_SIZE);

    p = tcallocdz(sizeof(*p), NULL, mad_free);
    p->input = decode;
    p->probe = probe;
    p->flush = mad_flush;
    p->private = md;

    return p;
}
