/**
    Copyright (C) 2003-2005  Michael Ahlberg, Måns Rullgård

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

static inline uint32_t
getss32(url_t *f, int version)
{
    uint32_t v;
    url_getu32b(f, &v);
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

static u_char *
id3v2_getframe(url_t *f, uint32_t *fsize, int fflags)
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

static char *id3v2_tags[][2] = {
    { "TPE1", "artist" },
    { "TIT2", "title" },
    { "TALB", "album" },
    { "TCON", "genre" },
    { "TYER", "year" },
    { "TRCK", "track" },

    /* v2.2 tags */
    { "TP1", "artist" },
    { "TT2", "title" },
    { "TAL", "album" },
    { "TCO", "genre" },
    { "TYE", "year" },
    { "TRK", "track" },

    {}
};

extern int
id3v2_tag(url_t *f, muxed_stream_t *ms)
{
    off_t spos;
    char tag[4];
    uint16_t version;
    int flags;
    int32_t size, tsize;
    int minsize;

    spos = f->tell(f);

    if(f->read(tag, 1, 3, f) < 3)
        goto err;

    if(strncmp(tag, "ID3", 3))
        goto err;               /* FIXME: check for trailing tag */

    url_getu16b(f, &version);
    if(version >= 0x0500 || version < 0x0200){
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

    minsize = version > 2? 10: 6;

    while(size > minsize){
        uint32_t fsize, dsize;
        uint16_t fflags = 0;
        int dlen = 0;
        off_t pos;
        char tag[5];
        int i;

        if(version > 2){
            if(f->read(tag, 1, 4, f) < 4)
                break;
            tag[4] = 0;
            fsize = getss32(f, version);
            url_getu16b(f, &fflags);
        } else {
            if(f->read(tag, 1, 3, f) < 3)
                break;
            tag[3] = 0;
            fsize = url_getc(f) << 16;
            fsize += url_getc(f) << 8;
            fsize += url_getc(f);
        }

        dsize = fsize;
        pos = f->tell(f);

        tc2_print("ID3", TC2_PRINT_DEBUG, "tag %s size=%x flags=%x\n",
                  tag, fsize, fflags);

        if(fflags & ID3v2_FFLAG_GID)
            url_getc(f);
        if(fflags & ID3v2_FFLAG_CRYPT)
            url_getc(f);
        if(fflags & ID3v2_FFLAG_DLEN)
            dlen = getss32(f, version);

        for(i = 0; id3v2_tags[i][0]; i++)
            if(!strcmp(tag, id3v2_tags[i][0]))
                break;

        if(id3v2_tags[i][0]){
            u_char *data = id3v2_getframe(f, &dsize, fflags);
            char *text = id3v2_gettext(data, dsize);
            tcattr_set(ms, id3v2_tags[i][1], text, NULL, free);
            free(data);
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
    int tagsize;
    char *tag, *p;
    int i;

    tagsize = 10;

    for(i = 0; id3v2_tags[i][0][3]; i++){
        char *av = tcattr_get(ms, id3v2_tags[i][1]);
        if(av)
            tagsize += 11 + strlen(av);
    }

    if(tagsize <= 10)
        return 0;

    tag = malloc(tagsize + 4);
    p = tag;

    p += sprintf(tag, "ID3%c%c%c", 4, 0, 0);
    st_ss32(tagsize - 10, p);
    p += 4;

    for(i = 0; id3v2_tags[i][0][3]; i++){
        char *av = tcattr_get(ms, id3v2_tags[i][1]);
        if(av)
            p += id3v2_text_frame(p, id3v2_tags[i][0], av);
    }

    u->write(tag, 1, tagsize, u);

    free(tag);
    return 0;
}


static char *
id3v1_strdup(char *p, int s)
{
    char *e = p + s - 1;
    while(!(*e & ~0x20) && e > p)
        e--;
    s = e - p + 1;
    if(e == p && !(*e & ~0x20))
        return NULL;
    return conv_str(p, s, tcvp_demux_mp3_conf_id3v1_encoding, "UTF-8");
}

static int
id3v1_setattr(void *p, char *attr, char *s, int l)
{
    char *v;

    if(tcattr_get(p, attr))
        return 0;

    v = id3v1_strdup(s, l);
    if(!v)
        return 0;

    tcattr_set(p, attr, v, NULL, free);

    return 0;
}

extern int
id3v1_tag(url_t *f, muxed_stream_t *ms)
{
    uint64_t pos = f->tell(f);
    u_char buf[128];
    int ts = 0;

    f->seek(f, -128, SEEK_END);
    f->read(buf, 1, 128, f);
    if(!memcmp(buf, "TAG", 3)){
        int trk = buf[126];

        id3v1_setattr(ms, "title", buf + 3, 30);
        id3v1_setattr(ms, "artist", buf + 33, 30);
        id3v1_setattr(ms, "album", buf + 63, 30);
        id3v1_setattr(ms, "year", buf + 93, 4);
        id3v1_setattr(ms, "comment", buf + 97, 29);

        if(trk > 0 && trk < 100 && !(trk == 32 && buf[125] == 32) &&
           !tcattr_get(ms, "track")){
            char *track = malloc(4);
            snprintf(track, 4, "%i", trk);
            tcattr_set(ms, "track", track, NULL, free);
        }

        ts = 128;
    }

    f->seek(f, pos, SEEK_SET);
    return ts;
}
