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
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcbyteswap.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <tcvp_event.h>
#include <mp3_tc2.h>

typedef struct mp3_file {
    url_t* file;
    stream_t stream;
    int used;
    off_t start;
    size_t size;
    eventq_t qs;
    uint64_t sbr;
    size_t bytes;
} mp3_file_t;

typedef struct mp3_frame {
    int version;
    int layer;
    int bitrate;
    int sample_rate;
    int size;
} mp3_frame_t;

#define get4c(t,f) f->read(&t, 4, 1, f)

#define getuint(s)				\
static inline uint##s##_t			\
getu##s(url_t *f)				\
{						\
    uint##s##_t v;				\
    f->read(&v, sizeof(v), 1, f);		\
    v = htob_##s(v);				\
    return v;					\
}

getuint(16)
getuint(32)
getuint(64)

static inline uint32_t
getss32(url_t *f)
{
    uint32_t v;
    v = getu32(f);
    v = (v & 0x7fffff) | ((v & ~0xffffff) >> 1);
    v = (v & 0x7fff) | ((v & ~0xffff) >> 1);
    v = (v & 0x7f) | ((v & ~0xff) >> 1);
    return v;
}

#define ID3v2_FLAG_USYNC (1<<7)
#define ID3v2_FLAG_EXTH  (1<<6)
#define ID3v2_FLAG_EXP   (1<<5)
#define ID3v2_FLAG_FOOT  (1<<4)

#define ID3v2_FFLAG_GID   (1<<6)
#define ID3v2_FFLAG_COMPR (1<<3)
#define ID3v2_FFLAG_CRYPT (1<<2)
#define ID3v2_FFLAG_USYNC (1<<1)
#define ID3v2_FFLAG_DLEN  (1<<0)

#define TAG(a,b,c,d) (d + (c << 8) + (b << 16) + (a << 24))

static inline char *
tag2str(char *s, uint32_t tag)
{
    s[0] = (tag >> 24) & 0xff;
    s[1] = (tag >> 16) & 0xff;
    s[2] = (tag >> 8) & 0xff;
    s[3] = tag & 0xff;
    s[4] = 0;
    return s;
}

static char *
id3v2_getframe(url_t *f, int *fsize, int fflags)
{
    u_char *buf;

    buf = malloc(*fsize);
    f->read(buf, 1, *fsize, f);

    if(fflags & ID3v2_FFLAG_USYNC){
	u_char *s, *d;
	s = d = buf;
	while(s - buf < *fsize){
	    *(d++) = *s;
	    if(*(s++) == 0xff && *s == 0){
		s++;
	    }
	}
	*fsize = d - buf;
    }

    return buf;
}

static char *
id3v2_gettext(char *buf, int size)
{
    char *text;

    if(*buf){
	fprintf(stderr, "MP3: Unknown ID3v2 encoding %i\n", *buf);
	return NULL;
    }

    text = malloc(size--);
    strncpy(text, buf+1, size);
    text[size] = 0;

    return text;
}

static int
id3v2_tag(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    url_t *f = mf->file;
    off_t spos;
    char tag[4];
    int version;
    int flags;
    int32_t size, tsize;

    spos = f->tell(f);

    if(f->read(tag, 1, 3, f) < 3)
	goto err;

    if(strncmp(tag, "ID3", 3))
	goto err;		/* FIXME: check for trailing tag */

    version = getu16(f);
    if(version >= 0x0500 || version < 0x0300){
	fprintf(stderr, "MP3: Unsupported ID3v2 tag version %i.%i.\n",
		version >> 8, version & 0xff);
	
	goto err;
    }

    flags = url_getc(f);
    if(flags & 0xf){
	fprintf(stderr, "MP3: Unknown ID3v2 flags %x\n", flags);
	goto err;
    }

    size = getss32(f);
    tsize = size + ((flags & ID3v2_FLAG_FOOT)? 20: 10);
    mf->size -= tsize;

#ifdef DEBUG
    fprintf(stderr, "MP3: ID3v2 size=%x, flags=%x\n", size, flags);
#endif

    if(flags & ID3v2_FLAG_EXTH){
	uint32_t esize = getss32(f);
	f->seek(f, esize - 4, SEEK_CUR);
	size -= esize;
    }

    while(size > 0){
	uint32_t tag = getu32(f);
	uint32_t fsize, dsize;
	int fflags, dlen = 0;
	char *data = NULL;
	off_t pos;

	if(!tag)
	    break;

	dsize = fsize = getss32(f);
	fflags = getu16(f);
	pos = f->tell(f);

#ifdef DEBUG
	char stag[5];
	fprintf(stderr, "MP3: %s size=%i flags=%x\n",
		tag2str(stag, tag), fsize, fflags);
#endif

	if(fflags & ID3v2_FFLAG_GID)
	    url_getc(f);
	if(fflags & ID3v2_FFLAG_CRYPT)
	    url_getc(f);
	if(fflags & ID3v2_FFLAG_DLEN)
	    dlen = getss32(f);

	switch(tag){
	case TAG('T','I','T','2'):
	    data = id3v2_getframe(f, &dsize, fflags);
	    ms->title = id3v2_gettext(data, dsize);
	    free(data);
	    break;
	case TAG('T','P','E','1'):
	    data = id3v2_getframe(f, &dsize, fflags);
	    ms->performer = id3v2_gettext(data, dsize);
	    free(data);
	    break;
	}

	f->seek(f, pos + fsize, SEEK_SET);
	size -= fsize + 10;
    }

    f->seek(f, spos + tsize, SEEK_SET);
    return 0;

err:
    f->seek(f, spos, SEEK_SET);
    return -1;
}

static char *
id3v1_strdup(char *p, int s)
{
    char *e = p + s - 1, *r;
    while(!(*e & ~0x20) && e > p)
	e--;
    s = e - p + 1;
    r = malloc(s + 1);
    strncpy(r, p, s);
    r[s] = 0;
    return r;
}

static int
id3v1_tag(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    url_t *f = mf->file;
    off_t pos = f->tell(f);
    char buf[128];

    f->seek(f, -128, SEEK_END);
    f->read(buf, 1, 128, f);
    if(!strncmp(buf, "TAG", 3)){
	if(!ms->title)
	    ms->title = id3v1_strdup(buf + 3, 30);
	if(!ms->performer)
	    ms->performer = id3v1_strdup(buf + 33, 30);
	mf->size -= 128;
    }

    f->seek(f, pos, SEEK_SET);
    return 0;
}

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

    return time;
}

typedef struct mp3_packet {
    packet_t pk;
    u_char *data;
    int size;
} mp3_packet_t;

static void
mp3_free_pk(packet_t *p)
{
    mp3_packet_t *mp = (mp3_packet_t *) p;
    free(mp->data);
    free(mp);
}

static packet_t *
mp3_packet(muxed_stream_t *ms, int str)
{
    mp3_file_t *mf = ms->private;
    mp3_packet_t *mp;
    mp3_frame_t fr;
    int size = 1024;
    u_char *f;

    mp = calloc(1, sizeof(*mp));
    mp->data = malloc(size);
    mp->pk.stream = 0;
    mp->pk.data = &mp->data;
    mp->pk.sizes = &mp->size;
    mp->pk.planes = 1;
    mp->pk.free = mp3_free_pk;

    size = mf->file->read(mp->data, 1, size, mf->file);
    if(size <= 0){
	mp3_free_pk((packet_t *) mp);
	return NULL;
    }

    f = memchr(mp->data, 0xff, size - 2);
    if(f && !mp3_header(f[1], f[2], &fr) && fr.bitrate){
	if((fr.size < size - (f - mp->data) - 3) &&
	   f[fr.size] == 0xff &&
	   !mp3_header(f[fr.size+1], f[fr.size+2], NULL)){
	    u_int br;
	    mf->sbr += (uint64_t) size * fr.bitrate;
	    mf->bytes += size;
	    br = mf->sbr / mf->bytes;
	    if(br != mf->stream.audio.bit_rate){
#ifdef DEBUG
		fprintf(stderr, "MP3: bitrate %i [%u] @%lx\n",
			fr.bitrate, br,
			mf->file->tell(mf->file) - size+(f-mp->data));
#endif
		mf->stream.audio.bit_rate = br;
		ms->time = 27 * 8000000LL * mf->size / br;
		tcvp_event_t *te = tcvp_alloc_event(TCVP_STREAM_INFO);
		eventq_send(mf->qs, te);
		tcfree(te);
	    }	    
	}
    }

    mp->size = size;

    return &mp->pk;
}

static void
mp3_free(void *p)
{
    muxed_stream_t *ms = p;
    mp3_file_t *mf = ms->private;

    if(ms->file)
	free(ms->file);
    if(ms->title)
	free(ms->title);
    if(ms->performer)
	free(ms->performer);

    eventq_delete(mf->qs);

    mf->file->close(mf->file);
    free(mf);
}

extern muxed_stream_t *
mp3_open(char *name, conf_section *cs)
{
    muxed_stream_t *ms;
    mp3_file_t *mf;
    char *qname, *qn;
    url_t *f;

    if(!(f = url_open(name, "r")))
	return NULL;

    ms = tcallocd(sizeof(*ms), NULL, mp3_free);
    memset(ms, 0, sizeof(*ms));

    mf = calloc(1, sizeof(*mf));

    ms->n_streams = 1;
    ms->streams = &mf->stream;
    ms->file = strdup(name);
    ms->private = mf;

    mf->file = f;
    mf->stream.stream_type = STREAM_TYPE_AUDIO;
    mf->size = f->size;

    if(id3v2_tag(ms))
	if(!f->flags & URL_FLAG_STREAMED)
	    id3v1_tag(ms);

    if(mp3_getparams(ms)){
	tcfree(ms);
	return NULL;
    }

    conf_getvalue(cs, "qname", "%s", &qname);
    qn = alloca(strlen(qname) + 8);
    mf->qs = eventq_new(tcref);
    sprintf(qn, "%s/status", qname);
    eventq_attach(mf->qs, qn, EVENTQ_SEND);

    mf->start = f->tell(f);

    ms->used_streams = &mf->used;
    ms->next_packet = mp3_packet;
    ms->seek = mp3_seek;

    return ms;
}
