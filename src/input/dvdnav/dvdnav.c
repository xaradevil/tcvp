/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/



#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <tcstring.h>
#include <sys/types.h>
#include <stdint.h>
#include <tcalloc.h>
#include <dvdnav/dvdnav.h>
#include <dvdnav_tc2.h>

#define DVD_SECTOR_SIZE 2048

typedef struct dvd {
    dvdnav_t *dvd;
    int blocks;
    int angle;
    int64_t pos;
    char *buf;
    int bufsize;
    int bbytes;
    int bpos;
    int end;
} dvd_t;

#define min(a, b) ((a)<(b)?(a):(b))

static int
dvd_read(void *buf, size_t size, size_t count, url_t *u)
{
    dvd_t *d = u->private;
    size_t rbytes = size * count;
    size_t bytes = rbytes;

    if(d->end)
	return -1;

    while(bytes){
	int bb = min(bytes, d->bbytes - d->bpos);

/* 	fprintf(stderr, "DVD: reading %i bytes @%lli\n", bb, d->pos); */
	memcpy(buf, d->buf + d->bpos, bb);
	d->bpos += bb;
	d->pos += bb;
	bytes -= bb;

	if(d->bpos == d->bbytes){
	    int32_t event, len;
	    do {
		if(dvdnav_get_next_block(d->dvd, d->buf, &event, &len) !=
		   DVDNAV_STATUS_OK){
		    d->bbytes = 0;
		    d->bpos = 0;
		    break;
		}
		switch(event){
		case DVDNAV_BLOCK_OK:
		    break;
		case DVDNAV_STILL_FRAME:
		    dvdnav_still_skip(d->dvd);
		    break;
		case DVDNAV_WAIT:
		    dvdnav_wait_skip(d->dvd);
		case DVDNAV_STOP:
		    d->end = 1;
		    return -1;
/* 		default: */
/* 		    fprintf(stderr, "DVD: event %i\n", event); */
		}
	    } while(event != DVDNAV_BLOCK_OK);
	    d->bbytes = len;
	    d->bpos = 0;
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
    if((diff > 0 && diff <= d->bbytes - d->bpos) ||
       (diff < 0 && -diff <= d->bpos)){
	d->bpos += diff;
    } else {
	uint64_t sector = np / DVD_SECTOR_SIZE;
	if(dvdnav_sector_search(d->dvd, sector, SEEK_SET) != DVDNAV_STATUS_OK){
	    fprintf(stderr, "DVD: error seeking to sector %lli\n", sector);
	    return -1;
	}
	d->bbytes = 0;
	d->bpos = 0;
	np = sector * DVD_SECTOR_SIZE;
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

    dvdnav_close(d->dvd);
    free(d->buf);
    free(d);
    tcfree(u);

    return 0;
}

extern url_t *
dvd_open(char *url, char *mode)
{
    int title = url_dvd_conf_title;
    int angle = url_dvd_conf_angle;
    int chapter = 1;
    int32_t titles, angles, chapters, ca;
    uint32_t pos, len;
    char *device = url_dvd_conf_device;
    dvdnav_t *dvd;
    char *p, *tmp;
    dvd_t *d;
    url_t *u;

    if(strcmp(mode, "r"))
	return NULL;

    if(!strncmp(url, "dvd:", 4))
	url += 4;

    url = strdup(url);

    if((tmp = strchr(url, '?')))
	*tmp++ = 0;

    while((p = strsep(&tmp, "&"))){
	if(!*p)
	    continue;
	if(!strncmp(p, "title=", 6)){
	    title = strtol(p + 6, NULL, 0);
	} else if(!strncmp(p, "angle=", 6)){
	    angle = strtol(p + 6, NULL, 0);
	} else if(!strncmp(p, "chapter=", 8)){
	    chapter = strtol(p + 8, NULL, 0);
	}
    }

    if(*url)
	device = url;

    if(!device)
	return NULL;

    if(dvdnav_open(&dvd, device) != DVDNAV_STATUS_OK){
	fprintf(stderr, "DVD: can't open %s\n", device);
	goto err;
    }

    dvdnav_set_PGC_positioning_flag(dvd, 1);

    dvdnav_get_number_of_titles(dvd, &titles);
    fprintf(stderr, "DVD: %i titles.\n", titles);
    if(title < 1 || title > titles){
	fprintf(stderr, "DVD: invalid title %i.\n", title);
	goto err;
    }

    if(dvdnav_get_number_of_parts(dvd, title, &chapters) != DVDNAV_STATUS_OK){
	fprintf(stderr, "DVD: error getting number of chapters.\n");
	goto err;
    }

    fprintf(stderr, "DVD: %i chapters.\n", chapters);
    if(chapter < 1 || chapter > chapters){
	fprintf(stderr, "DVD: invalid chapter %i.\n", chapter);
	goto err;
    }

    if(dvdnav_part_play(dvd, title, chapter) != DVDNAV_STATUS_OK){
	fprintf(stderr, "DVD: error playing title %i, chapter %i\n",
		title, chapter);
	goto err;
    }

    dvdnav_get_angle_info(dvd, &ca, &angles);
    fprintf(stderr, "DVD: %i angles.\n", angles);
    if(angle < 1 || angle > angles){
	fprintf(stderr, "DVD: invalid angle %i.\n", angle);
	goto err;
    }

    if(dvdnav_angle_change(dvd, angle) != DVDNAV_STATUS_OK){
	fprintf(stderr, "DVD: error setting angle %i\n", angle);
	goto err;
    }

    if(dvdnav_get_position_in_title(dvd, &pos, &len) != DVDNAV_STATUS_OK){
	fprintf(stderr, "DVD: error getting size.\n");
	goto err;
    }

    d = calloc(1, sizeof(*d));
    d->dvd = dvd;
    d->angle = angle;
    d->bufsize = DVD_SECTOR_SIZE;
    d->buf = malloc(d->bufsize);

    u = tcallocz(sizeof(*u));
    u->read = dvd_read;
    u->seek = dvd_seek;
    u->tell = dvd_tell;
    u->close = dvd_close;
    u->private = d;
    u->size = (uint64_t) len * DVD_SECTOR_SIZE;

    free(url);
    return u;

err:
    if(dvd)
	dvdnav_close(dvd);
    free(url);
    return NULL;
}
