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
getss32(url_t *f, int version)
{
    uint32_t v;
    v = getu32(f);
    if(version > 3){
	v = (v & 0x7fffff) | ((v & ~0xffffff) >> 1);
	v = (v & 0x7fff) | ((v & ~0xffff) >> 1);
	v = (v & 0x7f) | ((v & ~0xff) >> 1);
    }
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

extern int
id3v2_tag(url_t *f, muxed_stream_t *ms)
{
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

    version >>= 8;
    size = getss32(f, 4);
    tsize = size + ((flags & ID3v2_FLAG_FOOT)? 20: 10);

#ifdef DEBUG
    fprintf(stderr, "MP3: ID3v2 size=%x, flags=%x\n", size, flags);
#endif

    if(flags & ID3v2_FLAG_EXTH){
	uint32_t esize = getss32(f, version);
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

	dsize = fsize = getss32(f, version);
	fflags = getu16(f);
	pos = f->tell(f);

#ifdef DEBUG
	char stag[5];
	fprintf(stderr, "MP3: %s size=%x flags=%x\n",
		tag2str(stag, tag), fsize, fflags);
#endif

	if(fflags & ID3v2_FFLAG_GID)
	    url_getc(f);
	if(fflags & ID3v2_FFLAG_CRYPT)
	    url_getc(f);
	if(fflags & ID3v2_FFLAG_DLEN)
	    dlen = getss32(f, version);

	switch(tag){
	case TAG('T','I','T','2'):
	    data = id3v2_getframe(f, &dsize, fflags);
	    tcattr_set(ms, "title", id3v2_gettext(data, dsize), NULL, free);
	    free(data);
	    break;
	case TAG('T','P','E','1'):
	    data = id3v2_getframe(f, &dsize, fflags);
	    tcattr_set(ms, "performer", id3v2_gettext(data, dsize),
		       NULL, free);
	    free(data);
	    break;
	}

	f->seek(f, pos + fsize, SEEK_SET);
	size -= fsize + 10;
    }

    f->seek(f, spos + tsize, SEEK_SET);
    return tsize;

err:
    f->seek(f, spos, SEEK_SET);
    return -1;
}

static void
st_ss32(uint32_t v, void *d)
{
    v = (v & 0xff) | ((v << 1) & 0xffffff00);
    v = (v & 0xffff) | ((v << 1) & 0xffff0000);
    v = (v & 0xffffff) | ((v << 1) & 0xff000000);
    st_unaligned32(htob_32(v), d);
}

static int
id3v2_text_frame(char *tag, char *id, char *text)
{
    int size = strlen(text);

    strcpy(tag, id);
    tag += 4;
    st_ss32(size + 1, tag);
    tag += 4;
    *tag++ = 0;
    *tag++ = 0;
    *tag++ = 0;
    strcpy(tag, text);

    return size + 11;
}

extern int
id3v2_write_tag(url_t *u, muxed_stream_t *ms)
{
    char *artist, *title, *album;
    int tagsize;
    char *tag, *p;

    artist = tcattr_get(ms, "artist");
    title = tcattr_get(ms, "title");
    album = tcattr_get(ms, "album");

    tagsize = 10;
    if(artist)
	tagsize += 11 + strlen(artist);
    if(title)
	tagsize += 11 + strlen(title);
    if(album)
	tagsize += 11 + strlen(album);

    if(tagsize <= 10)
	return 0;

    tag = malloc(tagsize);
    p = tag;

    p += sprintf(tag, "ID3%c%c%c", 4, 0, 0);
    st_ss32(tagsize, p);
    p += 4;

    if(artist)
	p += id3v2_text_frame(p, "TPE1", artist);
    if(title)
	p += id3v2_text_frame(p, "TIT2", title);
    if(album)
	p += id3v2_text_frame(p, "TALB", album);

    u->write(tag, 1, tagsize, u);
    return 0;
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

extern int
id3v1_tag(url_t *f, muxed_stream_t *ms)
{
    uint64_t pos = f->tell(f);
    char buf[128];
    int ts = 0;

    f->seek(f, -128, SEEK_END);
    f->read(buf, 1, 128, f);
    if(!strncmp(buf, "TAG", 3)){
	if(!tcattr_get(ms, "title"))
	    tcattr_set(ms, "title", id3v1_strdup(buf + 3, 30), NULL, free);
	if(!tcattr_get(ms, "performer"))
	    tcattr_set(ms, "performer", id3v1_strdup(buf + 33, 30),
		       NULL, free);
	ts = 128;
    }

    f->seek(f, pos, SEEK_SET);
    return ts;
}
