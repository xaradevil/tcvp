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
#include <sys/stat.h>
#include <mp3_tc2.h>

typedef struct mp3_file {
    FILE* file;
    stream_t stream;
    int used;
    off_t start;
    size_t size;
} mp3_file_t;

#define get4c(t,f) fread(&t, 4, 1, f)

#define getuint(s)				\
static inline uint##s##_t			\
getu##s(FILE *f)				\
{						\
    uint##s##_t v;				\
    fread(&v, sizeof(v), 1, f);			\
    v = htob_##s(v);				\
    return v;					\
}

getuint(16)
getuint(32)
getuint(64)

static inline uint32_t
getss32(FILE *f)
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
id3v2_getframe(FILE *f, int *fsize, int fflags)
{
    u_char *buf;

    buf = malloc(*fsize);
    fread(buf, 1, *fsize, f);

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
	fprintf(stderr, "MP3: Unknowd ID3v2 encoding %i\n", *buf);
	return NULL;
    }

    text = malloc(--size);
    strncpy(text, buf+1, size);
    text[size] = 0;

    return text;
}

static int
id3v2_tag(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    off_t spos;
    char tag[4];
    int version;
    int flags;
    int32_t size, tsize;

    spos = ftell(mf->file);

    if(fread(tag, 1, 3, mf->file) < 3)
	goto err;

    if(strncmp(tag, "ID3", 3))
	goto err;		/* FIXME: check for trailing tag */

    version = getu16(mf->file);
    if(version >= 0x0500 || version < 0x0300){
	fprintf(stderr, "MP3: Unsupported ID3v2 tag version %i.%i.\n",
		version >> 8, version & 0xff);
	
	goto err;
    }

    flags = getc(mf->file);
    if(flags & 0xf){
	fprintf(stderr, "MP3: Unknown ID3v2 flags %x\n", flags);
	goto err;
    }

    size = getss32(mf->file);
    tsize = size + (flags & ID3v2_FLAG_FOOT)? 20: 10;
    mf->size -= tsize;

    if(flags & ID3v2_FLAG_EXTH){
	uint32_t esize = getss32(mf->file);
	fseek(mf->file, esize - 4, SEEK_CUR);
	size -= esize;
    }

    while(size > 0){
	uint32_t tag = getu32(mf->file);
	uint32_t fsize, dsize;
	int fflags, dlen = 0;
	char *data = NULL;
	off_t pos;

	dsize = fsize = getss32(mf->file);
	fflags = getu16(mf->file);
	pos = ftell(mf->file);

	if(fflags & ID3v2_FFLAG_GID)
	    getc(mf->file);
	if(fflags & ID3v2_FFLAG_CRYPT)
	    getc(mf->file);
	if(fflags & ID3v2_FFLAG_DLEN)
	    dlen = getss32(mf->file);

	switch(tag){
	case TAG('T','I','T','2'):
	    data = id3v2_getframe(mf->file, &dsize, fflags);
	    ms->title = id3v2_gettext(data, dsize);
	    free(data);
	    break;
	case TAG('T','P','E','1'):
	    data = id3v2_getframe(mf->file, &dsize, fflags);
	    ms->performer = id3v2_gettext(data, dsize);
	    free(data);
	    break;
	}

	fseek(mf->file, pos + fsize, SEEK_SET);
	size -= fsize + 10;
    }

    fseek(mf->file, spos + tsize, SEEK_SET);
    return 0;

err:
    fseek(mf->file, spos, SEEK_SET);
    return -1;
}

static char *
id3v1_strdup(char *p, int s)
{
    char *e = p + s - 1, *r;
    while(!(*e & ~0x20))
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
    off_t pos = ftell(mf->file);
    char buf[128];

    fseek(mf->file, -128, SEEK_END);
    fread(buf, 1, 128, mf->file);
    if(!strncmp(buf, "TAG", 3)){
	if(!ms->title)
	    ms->title = id3v1_strdup(buf + 3, 30);
	if(!ms->performer)
	    ms->performer = id3v1_strdup(buf + 33, 30);
	mf->size -= 128;
    }

    fseek(mf->file, pos, SEEK_SET);
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
mp3_getparams(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    int c = -1, d = -1, i, bx;
    int layer = 0, version = 0, br, pad, sr;
    int brate = 0, srate, fsize;
    int hdrok = 0;
    off_t pos = 0;

    for(i = 0; i < 8065; i++){
	if(getc(mf->file) == 0xff){
	    c = getc(mf->file);
	    if((c & 0xe0) != 0xe0 ||
	       ((c & 0x18) == 0x08 ||
		(c & 0x06) == 0)){
		hdrok = 0;
		continue;
	    }
	    d = getc(mf->file);
	    if((d & 0xf0) == 0xf0 ||
	       (d & 0x0c) == 0x0c){
		hdrok = 0;
		continue;
	    }
	    if(++hdrok == 2){
		fseek(mf->file, pos - 3, SEEK_SET);
		break;
	    }
	    pos = ftell(mf->file);

	    version = (c >> 3) & 0x3;
	    layer = 3 - ((c >> 1) & 0x3);
	    bx = layer + (layer == 2? ~version & 1: 0);
	    br = d >> 4;
	    sr = (d >> 2) & 3;
	    pad = (d >> 1) & 1;
	    brate = bitrates[br][bx] * 1000;
	    srate = sample_rates[sr][version];
	    fsize = 144 * brate / srate + pad;
	    fseek(mf->file, fsize - 3, SEEK_CUR);
	} else {
	    hdrok = 0;
	}
    }

    if(hdrok < 2)
	return -1;


    mf->stream.audio.bit_rate = brate;
    mf->stream.audio.codec = codecs[layer];
    if(brate)
	ms->time = 8000000LL * mf->size / brate;

    return 0;
}

static uint64_t
mp3_seek(muxed_stream_t *ms, uint64_t time)
{
    mp3_file_t *mf = ms->private;
    uint64_t pos;

    if(!mf->stream.audio.bit_rate)
	return -1LL;

    pos = time * mf->stream.audio.bit_rate / 8000000;

    if(pos > mf->size)
	return -1LL;

    fseek(mf->file, mf->start + pos, SEEK_SET);
    if(!mp3_getparams(ms))
	if(mf->stream.audio.bit_rate)
	    time = pos * 8000000LL / mf->stream.audio.bit_rate;

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
    int size = 4096;

    mp = calloc(1, sizeof(*mp));
    mp->data = malloc(size);
    mp->pk.data = &mp->data;
    mp->pk.sizes = &mp->size;
    mp->pk.planes = 1;
    mp->pk.free = mp3_free_pk;

    size = fread(mp->data, 1, size, mf->file);
    if(size <= 0){
	mp3_free_pk((packet_t *) mp);
	return NULL;
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

    fclose(mf->file);
    free(mf);
}

extern muxed_stream_t *
mp3_open(char *name, conf_section *cs)
{
    muxed_stream_t *ms;
    mp3_file_t *mf;
    struct stat st;
    FILE *f;

    if(!(f = fopen(name, "r")))
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
    fstat(fileno(f), &st);
    mf->size = st.st_size;

    if(id3v2_tag(ms))
	id3v1_tag(ms);

    if(mp3_getparams(ms)){
	tcfree(ms);
	return NULL;
    }

    mf->start = ftell(f);

    ms->used_streams = &mf->used;
    ms->next_packet = mp3_packet;
    ms->seek = mp3_seek;

    return ms;
}
