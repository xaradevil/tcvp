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
#include <tcalloc.h>
#include <tcvp_types.h>
#include <mp3_tc2.h>
#include "id3.h"

#define MAX_FRAME_SIZE 8065
#define MAX_HEADER_SIZE 6

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
    u_char *buf;
    int bufsize;
    int bhead, btail;
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

static int mpeg_sample_rates[3][4] = {
    {11025, 0, 22050, 44100},
    {12000, 0, 24000, 48000},
    { 8000, 0, 16000, 32000}
};

static int aac_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000
};

static int ac3_sample_rates[4] = {
    48000, 44100, 32000, 0
};

static int ac3_frame_sizes[64][3] = {
    { 64,   69,   96   },  
    { 64,   70,   96   },  
    { 80,   87,   120  },  
    { 80,   88,   120  },  
    { 96,   104,  144  },  
    { 96,   105,  144  },  
    { 112,  121,  168  }, 
    { 112,  122,  168  }, 
    { 128,  139,  192  }, 
    { 128,  140,  192  }, 
    { 160,  174,  240  }, 
    { 160,  175,  240  }, 
    { 192,  208,  288  }, 
    { 192,  209,  288  }, 
    { 224,  243,  336  }, 
    { 224,  244,  336  }, 
    { 256,  278,  384  }, 
    { 256,  279,  384  }, 
    { 320,  348,  480  }, 
    { 320,  349,  480  }, 
    { 384,  417,  576  }, 
    { 384,  418,  576  }, 
    { 448,  487,  672  }, 
    { 448,  488,  672  }, 
    { 512,  557,  768  }, 
    { 512,  558,  768  }, 
    { 640,  696,  960  }, 
    { 640,  697,  960  }, 
    { 768,  835,  1152 }, 
    { 768,  836,  1152 }, 
    { 896,  975,  1344 }, 
    { 896,  976,  1344 }, 
    { 1024, 1114, 1536 },
    { 1024, 1115, 1536 },
    { 1152, 1253, 1728 },
    { 1152, 1254, 1728 },
    { 1280, 1393, 1920 },
    { 1280, 1394, 1920 },
};

static int ac3_bitrates[64] = {
    32, 32, 40, 40, 48, 48, 56, 56, 64, 64, 80, 80, 96, 96, 112, 112,
    128, 128, 160, 160, 192, 192, 224, 224, 256, 256, 320, 320, 384,
    384, 448, 448, 512, 512, 576, 576, 640, 640,
};

static char *codecs[] = {
    "audio/mp1",
    "audio/mp2",
    "audio/mp3",
    "audio/aac",
    "audio/ac3"
};

static int
mp3_header(u_char *head, mp3_frame_t *mf)
{
    int c = head[1], d = head[2];
    int bx, br, sr, pad;

    if(head[0] != 0xff)
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
    mf->sample_rate = mpeg_sample_rates[sr][mf->version];
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

    if(head[0] != 0xff)
	return -1;

    if((head[1] & 0xf6) != 0xf0)
	return -1;

    if(!mf)
	return 0;

    profile = head[2] >> 6;
    mf->sample_rate = aac_sample_rates[(head[2] >> 2) & 0xf];
    mf->size = ((int) (head[3] & 0x3) << 11) +
	((int) head[4] << 3) +
	((int) (head[5] & 0xe0) >> 5);
    mf->samples = 1024;
    mf->bitrate = mf->size * 8 * mf->sample_rate / mf->samples;
    mf->layer = 3;

    return 0;
}

static int
ac3_header(u_char *head, mp3_frame_t *mf)
{
    int fscod, frmsizecod;

    if(head[0] != 0x0b || head[1] != 0x77)
	return -1;

    fscod = (head[4] >> 6) & 0x3;
    frmsizecod = head[4] & 0x3f;

    if(!ac3_sample_rates[fscod])
	return -1;

    mf->sample_rate = ac3_sample_rates[fscod];
    mf->bitrate = ac3_bitrates[frmsizecod] * 1000;
    mf->size = ac3_frame_sizes[frmsizecod][fscod] * 2;
    mf->samples = 6 * 256;

    mf->layer = 4;

    return 0;
}

struct header_parser {
    int (*parser)(u_char *, mp3_frame_t *);
    int header_size;
    char *tag;
} header_parsers[] = {
    { mp3_header, 3, "MP3" },
    { aac_header, 6, "AAC" },
    { ac3_header, 5, "AC3" },
};

#define NUM_PARSERS (sizeof(header_parsers) / sizeof(header_parsers[0]))

static int
all_headers(mp3_file_t *mf, u_char *head, mp3_frame_t *fr)
{
    int i;

    if(mf->parse_header)
	return mf->parse_header(head, fr);

    for(i = 0; i < NUM_PARSERS; i++){
	if(!header_parsers[i].parser(head, fr)){
	    mf->parse_header = header_parsers[i].parser;
	    mf->header_size = header_parsers[i].header_size;
	    mf->tag = header_parsers[i].tag;
	    return 0;
	}
    }

    return -1;
}

static int
mp3_getparams(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    url_t *f = mf->file;
    mp3_frame_t fr;
    int i;
    off_t pos = f->tell(f);
    u_char head[MAX_HEADER_SIZE];

    if(f->read(head, 1, MAX_HEADER_SIZE, f) < mf->header_size)
	return -1;

    for(i = 0; i < MAX_FRAME_SIZE; i++){
	if(!all_headers(mf, head, &fr)){
	    off_t pos2 = f->tell(f);
	    u_char head2[MAX_HEADER_SIZE];
	    f->seek(f, pos + i + fr.size, SEEK_SET);
	    f->read(head2, 1, mf->header_size, f);
	    if(!mf->parse_header(head2, &fr)){
		f->seek(f, pos + i, SEEK_SET);
		break;
	    }
	    f->seek(f, pos2, SEEK_SET);
	    mf->parse_header = NULL;
	}

	memmove(head, head + 1, MAX_HEADER_SIZE - 1);
	if(f->read(head + MAX_HEADER_SIZE - 1, 1, 1, f) != 1)
	    return -1;
    }

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
    u_char *buf;
    int size;
} mp3_packet_t;

static void
mp3_free_pk(void *p)
{
    mp3_packet_t *mp = p;
    tcfree(mp->buf);
}

static int
fill_buffer(mp3_file_t *mf)
{
    u_char *data = mf->buf + mf->bhead;
    int size = mf->bufsize - mf->bhead;
    uint64_t pos;

    pos = mf->file->tell(mf->file);
    size = min(size, mf->size - pos + mf->start);
    size = mf->file->read(data, 1, size, mf->file);

    if(size > 0)
	mf->bhead += size;

    return size;
}

static mp3_packet_t *
make_packet(mp3_file_t *mf, int offset, int size)
{
    mp3_packet_t *mp;

    mp = tcallocdz(sizeof(*mp), NULL, mp3_free_pk);
    mp->data = mf->buf + offset;
    mp->buf = tcref(mf->buf);
    mp->size = size;
    mp->pk.stream = 0;
    mp->pk.data = &mp->data;
    mp->pk.sizes = &mp->size;
    mp->pk.planes = 1;
    mp->pk.flags = TCVP_PKT_FLAG_PTS;
    mp->pk.pts = mf->samples * 27000000LL / mf->stream.audio.sample_rate;

    return mp;
}

static packet_t *
mp3_packet(muxed_stream_t *ms, int str)
{
    mp3_file_t *mf = ms->private;
    mp3_packet_t *mp = NULL;
    mp3_frame_t fr;
    int size;
    int bh = 0;
    u_char *nb;
    int eof = 0;

    if(!mf->used)
	return NULL;

    while(!mp){
	if(mf->bhead < mf->bufsize){
	    if(fill_buffer(mf) <= 0)
		eof = 1;
	}

	fr.size = 0;
	while(mf->bhead - mf->btail >= mf->header_size){
	    if(!mf->parse_header(mf->buf + mf->btail, &fr)){
		u_int br;

		if(fr.size > mf->bhead - mf->btail)
		    break;

		mp = make_packet(mf, mf->btail, fr.size);

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
		    tc2_print(mf->tag, TC2_PRINT_DEBUG+1,
			      "bitrate %i [%u] %lli s @%llx\n",
			      fr.bitrate, br, ms->time / 27000000,
			      mf->file->tell(mf->file) -
			      (mf->bhead - mf->btail));
		}
		mf->btail += fr.size;
		break;
	    } else {
		if(!bh++){
		    u_char *h = mf->buf + mf->btail;
		    tc2_print(mf->tag, TC2_PRINT_WARNING,
			      "bad header %02x%02x%02x @ %llx\n",
			      h[0], h[1], h[2], mf->file->tell(mf->file) -
			      (mf->bhead - mf->btail));
		}
		mf->btail++;
	    }
	}

	size = mf->bhead - mf->btail;
	if(size < fr.size || size < mf->header_size){
	    if(eof && !mp)
		return NULL;
	    nb = tcalloc(mf->bufsize);
	    memcpy(nb, mf->buf + mf->btail, mf->bhead - mf->btail);
	    tcfree(mf->buf);
	    mf->buf = nb;
	    mf->bhead = size;
	    mf->btail = 0;
	}
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
    tcfree(mf->buf);
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
    }

    tc2_print("MP3", TC2_PRINT_DEBUG, "data start %x\n", f->tell(f));

    mf->header_size = MAX_HEADER_SIZE;

    if(mp3_getparams(ms)){
	tcfree(ms);
	return NULL;
    }

    mf->samples = (uint64_t) tcvp_demux_mp3_conf_starttime *
	mf->stream.audio.sample_rate / 1000;

    ms->next_packet = mp3_packet;
    ms->seek = mp3_seek;
    ms->used_streams = &mf->used;

    mf->start = f->tell(f);
    mf->size -= mf->start;

    tc2_print(mf->tag, TC2_PRINT_DEBUG, "data start %x\n", f->tell(f));

    xing_header(ms);

    mf->bufsize = MAX_FRAME_SIZE;
    mf->buf = tcalloc(mf->bufsize);

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
