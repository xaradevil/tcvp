/**
    Copyright (C) 2003, 2004  Michael Ahlberg, Måns Rullgård

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
#include <tclist.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <mp3_tc2.h>
#include "id3.h"

typedef struct mp3_frame {
    int version;
    int layer;
    int bitrate;
    int sample_rate;
    int size;
    int samples;
} mp3_frame_t;

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
    int header_size;
    u_char head[8];
    int hb;
    int fsize;
    int (*parse_header)(u_char *, mp3_frame_t *);
    int xtime;
    u_char *xing;
    char *tag;
} mp3_file_t;

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

static int aac_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000
};
    
static char *codecs[4] = {
    "audio/mp1",
    "audio/mp2",
    "audio/mp3",
    "audio/aac"
};

static int
mp3_header(u_char *head, mp3_frame_t *mf)
{
    int c = head[0], d = head[1];
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
    mf->samples = 1152;

#ifdef DEBUG
    tc2_print("MP3", TC2_PRINT_DEBUG, "layer %i, version %i, rate %i\n",
	      mf->layer, mf->version, mf->bitrate);
#endif

    return 0;
}

static int
aac_header(u_char *head, mp3_frame_t *mf)
{
    int profile;

    if((head[0] & 0xf6) != 0xf0)
	return -1;

    if(!mf)
	return 0;

    profile = head[1] >> 6;
    mf->sample_rate = aac_sample_rates[(head[1] >> 2) & 0xf];
    mf->size = ((int) (head[2] & 0x3) << 11) +
	((int) head[3] << 3) +
	((int) (head[4] & 0xe0) >> 5);
    mf->samples = 1024;
    mf->bitrate = mf->size * 8 * mf->sample_rate / mf->samples;
    mf->layer = 3;

    return 0;
}

static int
mp3_getparams(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    url_t *f = mf->file;
    mp3_frame_t fr;
    int i;
    int hdrok = 0;
    off_t pos = 0;

    for(i = 0; i < 8065; i++){
	if(url_getc(f) == 0xff){
	    char head[mf->header_size];
	    f->read(head, 1, mf->header_size, f);
	    if(mf->parse_header(head, &fr))
		continue;
	    if(++hdrok == 2){
		f->seek(f, pos - mf->header_size - 1, SEEK_SET);
		break;
	    }
	    pos = f->tell(f);
	    f->seek(f, fr.size - mf->header_size - 1, SEEK_CUR);
	} else {
	    hdrok = 0;
	}
    }

    if(hdrok < 2)
	return -1;

    if(!mf->stream.audio.bit_rate)
	mf->stream.audio.bit_rate = fr.bitrate;
    mf->stream.audio.codec = codecs[fr.layer];
    mf->stream.audio.sample_rate = fr.sample_rate;
    if(fr.bitrate && !ms->time)
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

    if(mf->xing){
	int xi = 100 * time / ms->time;
	pos = mf->xing[xi] * mf->size / 256;
	time = ms->time * xi / 100;
    } else {
	pos = time * mf->stream.audio.bit_rate / (27 * 8000000);
    }

    if(pos > mf->size)
	return -1LL;

    mf->file->seek(mf->file, mf->start + pos, SEEK_SET);
    if(!mp3_getparams(ms))
	if(mf->stream.audio.bit_rate && !mf->xtime)
	    time = pos * 27 * 8000000LL / mf->stream.audio.bit_rate;

    mf->samples = time / 27000000 * mf->stream.audio.sample_rate;
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

static mp3_packet_t *
read_packet(mp3_file_t *mf)
{
    int size = 1024;
    u_char *data = malloc(size);
    mp3_packet_t *mp;
    uint64_t pos;

    if(!mf->used)
	return NULL;

    pos = mf->file->tell(mf->file);
    size = min(size, mf->size - pos + mf->start);
    size = mf->file->read(data, 1, size, mf->file);

    if(size <= 0)
	return NULL;

    mp = tcallocdz(sizeof(*mp), NULL, mp3_free_pk);
    mp->data = data;
    mp->size = size;
    mp->pk.stream = 0;
    mp->pk.data = &mp->data;
    mp->pk.sizes = &mp->size;
    mp->pk.planes = 1;

    return mp;
}

static packet_t *
mp3_packet(muxed_stream_t *ms, int str)
{
    mp3_file_t *mf = ms->private;
    mp3_packet_t *mp;
    mp3_frame_t fr;
    int size;
    int bh = 0;
    u_char *f;

    if(!(mp = read_packet(mf)))
	return NULL;

    size = mp->size;
    mp->pk.flags = TCVP_PKT_FLAG_PTS;
    mp->pk.pts = mf->samples * 27000000 / mf->stream.audio.sample_rate;

    f = mp->data + mf->fsize;
    while(f - mp->data < size - mf->header_size){
	memcpy(mf->head + mf->hb, f + 1 + mf->hb, mf->header_size - mf->hb);
	mf->hb = 0;
	if((f < mp->data || f[0] == 0xff) &&
	   !mf->parse_header(mf->head, &fr)){
	    u_int br;
	    mf->samples += fr.samples;
	    mf->sbr += (uint64_t) fr.size * fr.bitrate;
	    mf->bytes += fr.size;
	    br = mf->sbr / mf->bytes;
	    if(br != mf->stream.audio.bit_rate){
		mf->stream.audio.bit_rate = br;
		if(!mf->xtime){
		    ms->time = 27 * 8000000LL * mf->size / br;
		    if(mf->qs)
			tcvp_event_send(mf->qs, TCVP_STREAM_INFO);
		}
#ifdef DEBUG
		tc2_print(mf->tag, TC2_PRINT_DEBUG,
			  "bitrate %i [%u] %lli s @%llx\n",
			  fr.bitrate, br, ms->time / 27000000,
			  mf->file->tell(mf->file) - size + (f - mp->data));
#endif
	    }
	    f += fr.size;
	    bh = 0;
	} else {
	    if(!bh++)
		tc2_print(mf->tag, TC2_PRINT_WARNING,
			  "bad header %02x%02x @ %llx\n",
			  mf->head[0], mf->head[1],
			  mf->file->tell(mf->file) - size +
			  (uint64_t) (f - mp->data));
	    f++;
	}
    }

    mf->fsize = f - mp->data - size;
    if(mf->fsize < 0){
	mf->hb = -mf->fsize - 1;
	memcpy(mf->head, f + 1, mf->hb);
    }

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
    if(mf->xing)
	free(mf->xing);
    free(mf);
}

#define XING_SIZE 512

static int
xing_header(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    u_char x[XING_SIZE], *xp;
    int i, flags;
    uint64_t fp = mf->file->tell(mf->file);

    if(mf->file->read(x, 1, XING_SIZE, mf->file) < XING_SIZE)
	return -1;

    mf->file->seek(mf->file, fp, SEEK_SET);

    for(i = 0; i < XING_SIZE; i++)
	if(!strncmp(x + i, "Xing", 4))
	    break;

    if(i == XING_SIZE)
	return -1;

    xp = x + i + 4;
    flags = xp[3];
    xp += 4;

    if(flags & 0x1){
	int frames = (xp[0] << 24) + (xp[1] << 16) + (xp[2] << 8) + xp[3];
	uint64_t samples = 1152 * frames;
	ms->time = 27000000 * samples / mf->stream.audio.sample_rate;
	mf->stream.audio.samples = samples;
	mf->stream.audio.bit_rate =
	    mf->size * 8 * mf->stream.audio.sample_rate / samples;
	mf->xtime = 1;
	xp += 4;
    }

    if(flags & 0x2)
	xp += 4;

    if(flags & 0x4){
	mf->xing = malloc(100);
	memcpy(mf->xing, xp, 100);
    }

    return 0;
}

extern muxed_stream_t *
mp3_open(char *name, url_t *f, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    mp3_file_t *mf;
    char *qname, *qn;
    u_char head[4];
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

    f->read(head, 1, 4, f);
    if(strncmp(head, "RIFF", 4)){
	f->seek(f, -4, SEEK_CUR);
    } else {
	f->seek(f, 44, SEEK_SET);
	f->read(head, 1, 4, f);
    }

    if(head[0] == 0xff &&
       (head[1] & 0xf6) == 0xf0){
	mf->header_size = 5;
	mf->parse_header = aac_header;
	mf->tag = "AAC";
    } else {
	mf->header_size = 2;
	mf->parse_header = mp3_header;
	mf->tag = "MP3";
    }

    if(mp3_getparams(ms)){
	tcfree(ms);
	return NULL;
    }

    ms->next_packet = mp3_packet;
    ms->seek = mp3_seek;
    ms->used_streams = &mf->used;

    mf->start = f->tell(f);
    mf->size -= mf->start;

    xing_header(ms);

    if(tcconf_getvalue(cs, "qname", "%s", &qname) > 0){
	qn = alloca(strlen(qname) + 8);
	mf->qs = eventq_new(NULL);
	sprintf(qn, "%s/status", qname);
	eventq_attach(mf->qs, qn, EVENTQ_SEND);
	free(qname);
    }

    return ms;
}

extern int
mp3_init(char *p)
{
    TCVP_STREAM_INFO = tcvp_event_get("TCVP_STREAM_INFO");

    return 0;
}
