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
#include <sys/types.h>
#include <stdint.h>
#include <dvdread/dvd_reader.h>
#include <dvd_tc2.h>

#define buf_blocks url_dvd_conf_buffer

typedef struct dvd {
    dvd_reader_t *dvd;
    dvd_file_t *file;
    int64_t pos;
    char *buf;
    int bufsize;
    int bbytes;
    int bpos;
} dvd_t;

#define min(a, b) ((a)<(b)?(a):(b))

static int
dvd_read(void *buf, size_t size, size_t count, url_t *u)
{
    dvd_t *d = u->private;
    size_t rbytes = size * count;
    size_t bytes = rbytes;

    while(bytes){
	int bb = min(bytes, d->bbytes - d->bpos);
	memcpy(buf, d->buf + d->bpos, bb);
	d->bpos += bb;
	d->pos += bb;
	bytes -= bb;

	if(d->bpos == d->bbytes){
	    int block = d->pos / DVD_VIDEO_LB_LEN;
	    int br = DVDReadBlocks(d->file, block, buf_blocks, d->buf);

	    if(br > 0){
		d->bbytes = DVD_VIDEO_LB_LEN * br;
		d->bpos = d->pos - block * DVD_VIDEO_LB_LEN;
	    } else {
		fprintf(stderr, "DVD: error reading %i blocks @%i\n", 
			buf_blocks, block);
		d->bbytes = 0;
		d->bpos = 0;
		break;
	    }
	}
    }

    return bytes < rbytes? (rbytes - bytes) / size: -1;
}

static int
dvd_seek(url_t *u, int64_t offset, int how)
{
    dvd_t *d = u->private;
    int64_t np, diff;

    switch(how){
    case SEEK_SET:
	np = offset;
	break;
    case SEEK_CUR:
	np = d->pos + offset;
	break;
    case SEEK_END:
	np = u->size - offset;
	break;
    default:
	return -1;
    }

    if(np < 0 || np > u->size)
	return -1;

    diff = np - d->pos;
    if((diff > 0 && diff < d->bbytes - d->bpos) ||
       (diff < 0 && -diff < d->bpos)){
	d->bpos += diff;
    } else {
	d->bbytes = 0;
	d->bpos = 0;
    }

    d->pos = np;

    return 0;
}

static uint64_t
dvd_tell(url_t *u)
{
    dvd_t *d = u->private;
    return d->pos;
}

static int
dvd_close(url_t *u)
{
    dvd_t *d = u->private;

    DVDCloseFile(d->file);
    DVDClose(d->dvd);
    free(d);
    free(u);

    return 0;
}

extern url_t *
dvd_open(char *url, char *mode)
{
    int title = url_dvd_conf_title;
    char *device = url_dvd_conf_device;
    dvd_reader_t *dvd;
    dvd_file_t *file;
    dvd_t *d;
    url_t *u;
    char *p;

    if(strcmp(mode, "r"))
	return NULL;

    if(!strncmp(url, "dvd:", 4))
	url += 4;

    url = strdupa(url);

    if((p = strchr(url, '?'))){
	*p++ = 0;
	if(!strncmp(p, "title=", 6)){
	    p += 6;
	    title = strtol(p, &p, 0);
	}
    }

    if(*url)
	device = url;

    if(!device)
	return NULL;

    if(!(dvd = DVDOpen(device))){
	fprintf(stderr, "DVD: can't open %s\n", device);
	return NULL;
    }

    if(!(file = DVDOpenFile(dvd, title, DVD_READ_TITLE_VOBS))){
	fprintf(stderr, "DVD: can't open title %i\n", title);
	DVDClose(dvd);
	return NULL;
    }

    d = calloc(1, sizeof(*d));
    d->dvd = dvd;
    d->file = file;
    d->bufsize = url_dvd_conf_buffer * DVD_VIDEO_LB_LEN;
    d->buf = malloc(d->bufsize);

    u = calloc(1, sizeof(*u));
    u->read = dvd_read;
    u->seek = dvd_seek;
    u->tell = dvd_tell;
    u->close = dvd_close;
    u->private = d;

    u->size = (uint64_t) DVDFileSize(d->file) * DVD_VIDEO_LB_LEN;

    return u;
}
