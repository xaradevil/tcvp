/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcbyteswap.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <mp3_tc2.h>
#include "id3.h"

typedef struct mp3_file {
    url_t* file;
    stream_t stream;
    int used;
    uint64_t start;
    size_t size;
    eventq_t qs;
    uint64_t sbr;
    size_t bytes;
    uint64_t samples;
    int fsize;
    char fh;
} mp3_file_t;

typedef struct mp3_frame {
    int version;
    int layer;
    int bitrate;
    int sample_rate;
    int size;
} mp3_frame_t;

#define min(a, b) ((a)<(b)?(a):(b))

static int TCVP_STREAM_INFO;

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

static char *codecs[3] = {
    "audio/mp1",
    "audio/mp2",
    "audio/mp3"
};

static int
mp3_header(int c, int d, mp3_frame_t *mf)
{
    int bx, br, sr, pad;

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

#ifdef DEBUG
    fprintf(stderr, "MP3: layer %i, version %i, rate %i\n",
	    mf->layer, mf->version, mf->bitrate);
#endif

    return 0;
}

static int
mp3_getparams(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    url_t *f = mf->file;
    mp3_frame_t fr;
    int c = -1, d = -1, i;
    int hdrok = 0;
    off_t pos = 0;

    for(i = 0; i < 8065; i++){
	if(url_getc(f) == 0xff){
	    c = url_getc(f);
	    d = url_getc(f);
	    if(mp3_header(c, d, &fr))
		continue;
	    if(++hdrok == 2){
		f->seek(f, pos - 3, SEEK_SET);
		break;
	    }
	    pos = f->tell(f);
	    f->seek(f, fr.size - 3, SEEK_CUR);
	} else {
	    hdrok = 0;
	}
    }

    if(hdrok < 2)
	return -1;

    mf->stream.audio.bit_rate = fr.bitrate;
    mf->stream.audio.codec = codecs[fr.layer];
    mf->stream.audio.sample_rate = fr.sample_rate;
    if(fr.bitrate)
	ms->time = 27 * 8000000LL * mf->size / fr.bitrate;

    return 0;
}

static uint64_t
mp3_seek(muxed_stream_t *ms, uint64_t time)
{
    mp3_file_t *mf = ms->private;
    uint64_t pos;

    if(!mf->stream.audio.bit_rate)
	return -1LL;

    pos = time * mf->stream.audio.bit_rate / (27 * 8000000);

    if(pos > mf->size)
	return -1LL;

    mf->file->seek(mf->file, mf->start + pos, SEEK_SET);
    if(!mp3_getparams(ms))
	if(mf->stream.audio.bit_rate)
	    time = pos * 27 * 8000000LL / mf->stream.audio.bit_rate;

    mf->samples = time * mf->stream.audio.sample_rate;
    mf->fsize = 0;

    return time;
}

typedef struct mp3_packet {
    packet_t pk;
    u_char *data;
    int size;
} mp3_packet_t;

static void
mp3_free_pk(void *p)
{
    mp3_packet_t *mp = p;
    free(mp->data);
}

static packet_t *
mp3_packet(muxed_stream_t *ms, int str)
{
    mp3_file_t *mf = ms->private;
    mp3_packet_t *mp;
    mp3_frame_t fr;
    int size = 1024;
    uint64_t pos;
    int bh = 0;
    u_char *f;

    mp = tcallocdz(sizeof(*mp), NULL, mp3_free_pk);
    mp->data = malloc(size);
    mp->pk.stream = 0;
    mp->pk.data = &mp->data;
    mp->pk.sizes = &mp->size;
    mp->pk.planes = 1;
    mp->pk.flags = TCVP_PKT_FLAG_PTS;
    mp->pk.pts = mf->samples * 27000000 / mf->stream.audio.sample_rate;

    pos = mf->file->tell(mf->file);
    size = min(size, mf->size - pos + mf->start);
    size = mf->file->read(mp->data, 1, size, mf->file);
    if(size <= 0){
	tcfree(mp);
	return NULL;
    }

    f = mp->data + mf->fsize;
    while(f - mp->data < size - 2){
	u_char fh1;

	if(f - mp->data < -1)
	    fh1 = mf->fh;
	else
	    fh1 = f[1];

	if(!mp3_header(fh1, f[2], &fr)){
	    u_int br;
	    mf->samples += 1152;
	    mf->sbr += (uint64_t) fr.size * fr.bitrate;
	    mf->bytes += fr.size;
	    br = mf->sbr / mf->bytes;
	    if(br != mf->stream.audio.bit_rate){
		mf->stream.audio.bit_rate = br;
		ms->time = 27 * 8000000LL * mf->size / br;
#ifdef DEBUG
		fprintf(stderr, "MP3: bitrate %i [%u] %lli s @%llx\n",
			fr.bitrate, br, ms->time / 27000000,
			mf->file->tell(mf->file) - size + (f - mp->data));
#endif
		tcvp_event_send(mf->qs, TCVP_STREAM_INFO);
	    }
	    f += fr.size;
	    bh = 0;
	} else {
	    if(!bh++)
		fprintf(stderr, "MP3: bad header %02x%02x @ %llx %llx\n",
			fh1, f[2], pos + (uint64_t) (f - mp->data), pos);
	    f++;
	}
    }

    mf->fsize = f - mp->data - size;
    if(mf->fsize < 0)
	mf->fh = mp->data[size - 1];
    mp->size = size;

    return &mp->pk;
}

static void
mp3_free(void *p)
{
    muxed_stream_t *ms = p;
    mp3_file_t *mf = ms->private;

    eventq_delete(mf->qs);
    if(mf->file)
	mf->file->close(mf->file);
    free(mf);
}

extern muxed_stream_t *
mp3_open(char *name, url_t *f, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    mp3_file_t *mf;
    char *qname, *qn;
    int ts = 0;

    ms = tcallocd(sizeof(*ms), NULL, mp3_free);
    memset(ms, 0, sizeof(*ms));

    mf = calloc(1, sizeof(*mf));

    ms->n_streams = 1;
    ms->streams = &mf->stream;
    ms->private = mf;

    mf->file = tcref(f);
    mf->stream.stream_type = STREAM_TYPE_AUDIO;
    mf->size = f->size;

    id3v2_tag(f, ms);
    if(!f->flags & URL_FLAG_STREAMED)
	ts = id3v1_tag(f, ms);

    if(ts > 0)
	mf->size -= ts;

    if(mp3_getparams(ms)){
	tcfree(ms);
	return NULL;
    }

    tcconf_getvalue(cs, "qname", "%s", &qname);
    qn = alloca(strlen(qname) + 8);
    mf->qs = eventq_new(tcref);
    sprintf(qn, "%s/status", qname);
    eventq_attach(mf->qs, qn, EVENTQ_SEND);
    free(qname);

    mf->start = f->tell(f);
    mf->size -= mf->start;

    ms->used_streams = &mf->used;
    ms->next_packet = mp3_packet;
    ms->seek = mp3_seek;

    return ms;
}

extern int
mp3_init(char *p)
{
    TCVP_STREAM_INFO = tcvp_event_get("TCVP_STREAM_INFO");

    return 0;
}
