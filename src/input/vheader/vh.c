/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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
#include <string.h>
#include <tcalloc.h>
#include <vheader_tc2.h>

typedef struct vheader {
    uint8_t *header;
    int hsize;
    int pos;
    url_t *url;
} vheader_t;

#define min(a, b) ((a)<(b)? (a): (b))

static int
vh_read(void *buf, size_t size, size_t count, url_t *u)
{
    vheader_t *vh = u->private;
    size_t bytes = size * count;
    size_t rbytes = 0;

    if(vh->pos < vh->hsize){
	size_t hb = min(bytes, vh->hsize - vh->pos);
	memcpy(buf, vh->header + vh->pos, hb);
	buf += hb;
	vh->pos += hb;
	rbytes += hb;
	bytes -= hb;
    }

    if(bytes && vh->url->read)
	rbytes += vh->url->read(buf, 1, bytes, vh->url);

    return rbytes / size;
}

static int
vh_seek(url_t *u, int64_t offset, int how)
{
    vheader_t *vh = u->private;
    int64_t pos;

    switch(how){
    case SEEK_SET:
	pos = offset;
	break;
    case SEEK_CUR:
	pos = vh->hsize + offset;
	if(vh->url->tell)
	    pos += vh->url->tell(vh->url);
	break;
    case SEEK_END:
	pos = u->size + offset;
	break;
    default:
	return -1;
    }

    if(pos > u->size)
	return -1;

    if(pos < vh->hsize){
	vh->pos = pos;
	if(vh->url->seek)
	    vh->url->seek(vh->url, 0, SEEK_SET);
    } else if(vh->url->seek){
	vh->url->seek(vh->url, pos - vh->hsize, SEEK_SET);
    }

    return 0;
}

static uint64_t
vh_tell(url_t *u)
{
    vheader_t *vh = u->private;

    if(vh->pos < vh->hsize)
	return vh->pos;

    return vh->url->tell(vh->url) + vh->hsize;
}

static int
vh_close(url_t *u)
{
    vheader_t *vh = u->private;
    int r = vh->url->close(vh->url);
    vh->url = NULL;
    tcfree(u);
    return r;
}

static void
vh_free(void *p)
{
    url_t *u = p;
    vheader_t *vh = u->private;
    if(vh->url)
	tcfree(vh->url);
    free(vh);
}

extern url_t *
vh_new(url_t *u, uint8_t *header, int hsize)
{
    url_t *vhu;
    vheader_t *vh;

    vh = calloc(1, sizeof(*vh));
    vh->header = header;
    vh->hsize = hsize;
    vh->url = u;

    vhu = tcallocdz(sizeof(*vhu), NULL, vh_free);
    vhu->size = u->size + hsize;
    vhu->flags = u->flags;
    vhu->read = vh_read;
    vhu->seek = vh_seek;
    vhu->tell = vh_tell;
    vhu->close = vh_close;
    vhu->private = vh;

    return vhu;
}
