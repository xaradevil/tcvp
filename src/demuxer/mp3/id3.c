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
#include <iconv.h>
#include <errno.h>
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

static char *encodings[256] = {
    [0] = "ISO-8859-1",
    [1] = "UTF-16",
    [2] = "UTF-16BE",
    [3] = "UTF-8"
};

static char *
conv_str(char *s, size_t size, char *fset, char *tset)
{
    char *text, *ct;
    iconv_t ic;
    size_t ib, ob;

    if(!s)
	return strdup("");

    tc2_print("ID3", TC2_PRINT_DEBUG,
	      "converting '%.*s' (%li) from %s to %s\n",
	      size,  s, size, fset, tset);

    ic = iconv_open(tset, fset);
    if(ic == (iconv_t) -1){
	tc2_print("ID3", TC2_PRINT_ERROR, "iconv_open: %s\n", strerror(errno));
	return NULL;
    }

    ib = size;
    size *= 2;
    ob = size;
    text = malloc(ob);
    ct = text;

    iconv(ic, NULL, NULL, &ct, &ob);

    while(ib > 0){
	if(iconv(ic, &s, &ib, &ct, &ob) == -1){
	    if(errno == E2BIG){
		int s = ct - text;
		text = realloc(text, size *= 2);
		ob = size - s;
		ct = text + s;
		tc2_print("ID3", TC2_PRINT_DEBUG,
			  "output buffer full, resizing to %li (%li)\n",
			  size, ob);
	    } else {
		tc2_print("ID3", TC2_PRINT_ERROR,
			  "iconv: %s\n", strerror(errno));
		break;
	    }
	}
    }

    ob = ct - text;
    text = realloc(text, ob + 1);
    text[ob] = 0;

    iconv_close(ic);
    return text;
}

static char *
id3v2_gettext(u_char *buf, int size)
{
    char *enc;

    if(tcvp_demux_mp3_conf_override_encoding){
	enc = tcvp_demux_mp3_conf_override_encoding;
    } else if(encodings[*buf]){
	enc = encodings[*buf];
    } else {
	tc2_print("ID3", TC2_PRINT_WARNING,
		  "Unknown ID3v2 encoding %i\n", *buf);
	return NULL;
    }

    return conv_str(buf + 1, size - 1, enc, "UTF-8");
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
	tc2_print("ID3", TC2_PRINT_ERROR,
		  "Unsupported ID3v2 tag version %i.%i.\n",
		  version >> 8, version & 0xff);
	goto err;
    }

    flags = url_getc(f);
    if(flags & 0xf){
	tc2_print("ID3", TC2_PRINT_ERROR, "Unknown ID3v2 flags %x\n", flags);
	goto err;
    }

    version >>= 8;
    size = getss32(f, 4);
    tsize = size + ((flags & ID3v2_FLAG_FOOT)? 20: 10);

    tc2_print("ID3", TC2_PRINT_DEBUG,
	      "ID3v2 size=%x, flags=%x\n", size, flags);

    if(flags & ID3v2_FLAG_EXTH){
	uint32_t esize = getss32(f, version);
	f->seek(f, esize - 4, SEEK_CUR);
	size -= esize;
    }

    while(size > 0){
	uint32_t tag = getu32(f);
	uint32_t fsize, dsize;
	int fflags, dlen = 0;
	u_char *data = NULL;
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
    *tag++ = 3;
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

    if(artist && !strlen(artist))
	artist = NULL;
    if(title && !strlen(title))
	title = NULL;
    if(album && !strlen(album))
	album = NULL;

    tagsize = 10;
    if(artist)
	tagsize += 11 + strlen(artist);
    if(title)
	tagsize += 11 + strlen(title);
    if(album)
	tagsize += 11 + strlen(album);

    if(tagsize <= 10)
	return 0;

    tag = malloc(tagsize + 4);
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
    char *e = p + s - 1;
    while(!(*e & ~0x20) && e > p)
	e--;
    s = e - p + 1;
    return conv_str(p, s, tcvp_demux_mp3_conf_id3v1_encoding, "UTF-8");
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
