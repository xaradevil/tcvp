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
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <mad.h>
#include <mad_tc2.h>

#define MAX_FRAME_SIZE 8065

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

typedef struct mad_packet {
    tcvp_data_packet_t pk;
    int16_t *data;
    int size;
} mad_packet_t;

typedef struct mad_dec {
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;
    int bs;
    int bufsize;
    u_char *buf;
    uint64_t npts;
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

    if(size >= mf->size + 4)
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

static mad_packet_t *
mad_alloc(int ch, int samples, int str)
{
    mad_packet_t *mp;

    mp = tcallocdz(sizeof(*mp), NULL, mad_free_pk);
    mp->size = samples * ch * 2;
    mp->data = malloc(mp->size);
    mp->pk.stream = str;
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
    mad_packet_t *mp;
    int16_t *dp;

    channels = pcm->channels;
    samples = pcm->length;
    left = pcm->samples[0];
    right = pcm->samples[1];

    mp = mad_alloc(channels, samples, stream);
    if(md->npts != -1LL){
        mp->pk.flags |= TCVP_PKT_FLAG_PTS;
        mp->pk.pts = md->npts;
        md->npts = -1LL;
    }

    dp = mp->data;

    while(samples--){
        int sample;

        sample = scale(*left++);
        *dp++ = sample;

        if(channels == 2){
            sample = scale(*right++);
            *dp++ = sample;
        }
    }

    tp->next->input(tp->next, (tcvp_packet_t *) mp);

    return 0;
}

static int
decode_frame(tcvp_pipe_t *p, u_char *data, int size)
{
    mad_dec_t *md = p->private;

    mad_stream_buffer(&md->stream, data, size);

    if(!mad_frame_decode(&md->frame, &md->stream)){
        mad_synth_frame(&md->synth, &md->frame);
        output(p, &md->synth.pcm, p->format.common.index);
    } else if(md->stream.error != MAD_ERROR_BUFLEN){
        tc2_print("MAD", TC2_PRINT_VERBOSE, "%s\n",
                  mad_stream_errorstr(&md->stream));
    }

    return md->stream.next_frame - data;
}

extern int
mad_decode(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    mad_dec_t *md = p->private;
    u_char *d;
    int size;

    if(!pk->data){
        p->next->input(p->next, (tcvp_packet_t *) pk);
        return 0;
    }

    d = pk->data[0];
    size = pk->sizes[0];

    if(pk->flags & TCVP_PKT_FLAG_PTS){
        md->npts = pk->pts;
        if(md->bs)
            md->npts -= 1152 * 27000000LL / p->format.audio.sample_rate;
    }

    while(size > 0){
        u_char *fd;
        int bs, fs;

        bs = min(size, md->bufsize - md->bs);
        memcpy(md->buf + md->bs, d, bs);
        d += bs;
        size -= bs;
        md->bs += bs;

        fd = md->buf;
        fs = md->bs;

        while((bs = decode_frame(p, fd, fs)) > 0){
            fd += bs;
            fs -= bs;
        }

        if(fs > 0){
            memmove(md->buf, fd, fs);
            md->bs = fs;
        }
    }

    tcfree(pk);
    return 0;
}

extern int
mad_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    mp3_frame_t mf;
    u_char *d = pk->data[0];
    int ds = pk->sizes[0];

    while(ds > 3 && mp3_header(d, &mf, ds)){
        d++;
        ds--;
    }

    if(ds <= 4){
        tcfree(pk);
        return PROBE_AGAIN;
    }

    p->format.audio.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->format.audio.sample_rate = mf.sample_rate;
    p->format.audio.channels = mf.channels;
    p->format.audio.bit_rate = mf.channels * mf.sample_rate * 16;

    s->audio.sample_rate = mf.sample_rate;
    s->audio.channels = mf.channels;
    s->audio.bit_rate = mf.bitrate;

    tcfree(pk);

    return PROBE_OK;
}

extern int
mad_flush(tcvp_pipe_t *p, int drop)
{
    mad_dec_t *md = p->private;

    if(drop){
        md->bs = 0;
    }

    return 0;
}

static void
mad_free(void *p)
{
    mad_dec_t *md = p;

    mad_synth_finish(&md->synth);
    mad_frame_finish(&md->frame);
    mad_stream_finish(&md->stream);
    free(md->buf);
}

extern int
mad_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
        tcvp_timer_t *t, muxed_stream_t *ms)
{
    mad_dec_t *md;

    md = tcallocdz(sizeof(*md), NULL, mad_free);
    mad_stream_init(&md->stream);
    mad_stream_options(&md->stream, MAD_OPTION_IGNORECRC);
    mad_frame_init(&md->frame);
    mad_synth_init(&md->synth);
    md->bufsize = 2 * MAX_FRAME_SIZE;
    md->buf = malloc(md->bufsize);
    md->npts = -1LL;

    p->format.audio.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->private = md;

    return 0;
}
