/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/



#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcbyteswap.h>
#include <stdint.h>
#include <tcalloc.h>
#include <dvdnav/dvdnav.h>
#include <dvdnav_tc2.h>

#define DVD_SECTOR_SIZE 2048

typedef struct dvd {
    dvdnav_t *dvd;
    pci_t *pci;
    int64_t pts, pts_offset;
    int angle;
    int64_t pos;
    char *buf;
    int bufsize;
    int bbytes;
    int bpos;
    int still;
    int dvdnav;
    int end;
} dvd_t;

#define min(a, b) ((a)<(b)?(a):(b))

static void
write_pes(dvd_t *d, void *data, int16_t size)
{
    char *buf = d->buf + d->bbytes;
    char *p = buf;

    if(!d->dvdnav)
	return;

    *p++ = 0;
    *p++ = 0;
    *p++ = 1;
    *p++ = DVD_PESID;
    st_unaligned16(htob_16(size), p);
    p += 2;
    memcpy(p, data, size);
    d->bbytes += p - buf + size;
}

static void
set_ptsoffset(dvd_t *d, int64_t offset)
{
    dvd_ptsskip_t ptsskip = { DVD_PTSSKIP, offset };

    d->pts_offset = offset;
    write_pes(d, &ptsskip, sizeof(ptsskip));
}

static void
flush(dvd_t *d, int drop)
{
    dvd_flush_t flush = { DVD_FLUSH, drop };
    write_pes(d, &flush, sizeof(flush));
}

static int
dvd_read(void *buf, size_t size, size_t count, url_t *u)
{
    dvd_t *d = u->private;
    size_t rbytes = size * count;
    size_t bytes = rbytes;

    if(d->end)
	return -1;

    while(bytes){
	int32_t event, len;
	int bb;

	if(d->bpos == d->bbytes){
	    d->bpos = 0;
	    d->bbytes = 0;

	    while(!d->bbytes){
		if(dvdnav_get_next_block(d->dvd, d->buf, &event, &len) !=
		   DVDNAV_STATUS_OK){
		    d->bbytes = 0;
		    d->bpos = 0;
		    break;
		}
		switch(event){
		case DVDNAV_BLOCK_OK:
		    d->bbytes = len;
		    break;
		case DVDNAV_STILL_FRAME:
		    if(!d->still){
			int t = DVD_STILL;
			fprintf(stderr, "DVD: still\n");
			flush(d, 0);
			d->still = 1;
			d->pci = dvdnav_get_current_nav_pci(d->dvd);
			write_pes(d, &t, sizeof(t));
		    } else {
			usleep(100000);
		    }
		    break;
		case DVDNAV_WAIT:
		    fprintf(stderr, "DVD: wait\n");
/* 		    flush(d, 0); */
		    dvdnav_wait_skip(d->dvd);
		    break;
		case DVDNAV_STOP:
		    fprintf(stderr, "DVD: stop\n");
		    break;
		    d->end = 1;
		    return -1;
		case DVDNAV_NAV_PACKET: {
		    pci_t *pci = dvdnav_get_current_nav_pci(d->dvd);
		    if(d->pts && pci->pci_gi.vobu_s_ptm != d->pts){
			set_ptsoffset(d, d->pts_offset +
				      d->pts - pci->pci_gi.vobu_s_ptm);
		    }
		    d->pts = pci->pci_gi.vobu_e_ptm;
		    break;
		}
		case DVDNAV_HOP_CHANNEL:
		    fprintf(stderr, "DVD: hop_channel\n");
		    flush(d, 1);
		    break;
		case DVDNAV_VTS_CHANGE:
		    fprintf(stderr, "DVD: vts change\n");
/* 		    flush(d, 0); */
/* 		    set_ptsoffset(d, 0); */
/* 		    d->pts = 0; */
		    break;
		case DVDNAV_AUDIO_STREAM_CHANGE: {
		    dvdnav_audio_stream_change_event_t *asc =
			(dvdnav_audio_stream_change_event_t *) d->buf;
		    dvd_audio_id_t dai;
		    fprintf(stderr, "DVD: audio stream change %x %x\n",
			    asc->physical, asc->logical);
		    dai.type = DVD_AUDIO_ID;
		    dai.id = asc->logical & 0x80? asc->logical: 0x80;
		    write_pes(d, &dai, sizeof(dai));
		    break;
		}
		case DVDNAV_CELL_CHANGE:
		    fprintf(stderr, "DVD: cell change\n");
		    break;
		default:
		    fprintf(stderr, "DVD: event %i\n", event);
		    break;
		}
	    }
	}

	bb = min(bytes, d->bbytes - d->bpos);
/* 	fprintf(stderr, "DVD: reading %i bytes @%lli\n", bb, d->pos); */
	memcpy(buf, d->buf + d->bpos, bb);
	d->bpos += bb;
	d->pos += bb;
	bytes -= bb;

	if(event != DVDNAV_BLOCK_OK)
	    break;
    }

    return bytes < rbytes? (rbytes - bytes) / size: -1;
}

static void
dvd_button(url_t *u, int x, int y)
{
    dvd_t *d = u->private;
    pci_t *pci = d->pci;

    if(!pci)
	pci = dvdnav_get_current_nav_pci(d->dvd);
    dvdnav_mouse_activate(d->dvd, pci, x, y);
    dvdnav_still_skip(d->dvd);
    d->still = 0;
    d->pci = NULL;
}

static void
dvd_enable(url_t *u, int e)
{
    dvd_t *d = u->private;
    d->dvdnav = e;
}

static void
dvd_menu(url_t *u)
{
    dvd_t *d = u->private;
    dvdnav_menu_call(d->dvd, DVD_MENU_Escape);
    dvdnav_still_skip(d->dvd);
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
    d->pts = 0;

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
    int title = 0;
    int angle = 1;
    int chapter = 1;
    int32_t titles, angles, chapters, ca;
    uint32_t pos, len = 0;
    char *device = url_dvd_conf_device;
    dvd_functions_t *df;
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

    dvdnav_menu_language_select(dvd, url_dvd_conf_language);
    dvdnav_audio_language_select(dvd, url_dvd_conf_language);
    dvdnav_spu_language_select(dvd, url_dvd_conf_language);

    if(title > 0){
	dvdnav_get_number_of_titles(dvd, &titles);
	fprintf(stderr, "DVD: %i titles.\n", titles);
	if(title < 1 || title > titles){
	    fprintf(stderr, "DVD: invalid title %i.\n", title);
	    goto err;
	}

	if(dvdnav_get_number_of_parts(dvd, title, &chapters) !=
	   DVDNAV_STATUS_OK){
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

    df = calloc(1, sizeof(*df));
    df->button = dvd_button;
    df->enable = dvd_enable;
    df->menu = dvd_menu;
    tcattr_set(u, "dvd", df, NULL, free);

    free(url);
    return u;

err:
    if(dvd)
	dvdnav_close(dvd);
    free(url);
    return NULL;
}
