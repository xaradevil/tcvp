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
#include <unistd.h>
#include <tcstring.h>
#include <sys/types.h>
#include <stdint.h>
#include <tcalloc.h>
#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>
#include <dvdread/nav_read.h>
#include <dvd_tc2.h>

#define buf_blocks url_dvd_conf_buffer

#define CELL_START 1
#define CELL_LOOP  2
#define BLOCK_LOOP 3

typedef struct dvd_nav_point {
    int start;
    int blocks;
    int next;
    int tblocks;
} dvd_nav_point_t;

typedef struct dvd {
    dvd_reader_t *dvd;
    ifo_handle_t *ifo, *vts;
    dvd_file_t *file;
    pgc_t *pgc;
    int start_cell;
    int start_sector;
    int cell, next_cell, pack, npack;
    int blocks;
    int angle;
    int state;
    int64_t pos;
    char *buf;
    int bufsize;
    int bbytes;
    int bpos;
    vobu_admap_t *vobu_map;
} dvd_t;

#define min(a, b) ((a)<(b)?(a):(b))

static int
is_nav_pack(unsigned char *buffer)
{
    return buffer[41] == 0xbf && buffer[1027] == 0xbf;
}

static int
next_cell(pgc_t *pgc, int cell, int angle)
{
    if(pgc->cell_playback[cell].block_type == BLOCK_TYPE_ANGLE_BLOCK){
	int i;
	cell += angle;
	for(i = 0;; i++){
	    if(pgc->cell_playback[cell + i].block_mode ==
	       BLOCK_MODE_LAST_CELL){
		return cell + i + 1;
	    }
	}
    }

    return cell + 1;
}

static int
read_nav(dvd_file_t *df, int sector, int *next)
{
    char buf[DVD_VIDEO_LB_LEN];
    dsi_t dsi_pack;
    int blocks;

    if(DVDReadBlocks(df, sector, 1, buf) != 1){
	fprintf(stderr, "DVD: error reading NAV packet @%i\n", sector);
	return -1;
    }

    if(!is_nav_pack(buf))
	return -1;

    navRead_DSI(&dsi_pack, buf + DSI_START_BYTE);
    blocks = dsi_pack.dsi_gi.vobu_ea;

    if(dsi_pack.vobu_sri.next_vobu != SRI_END_OF_CELL){
	*next = sector + (dsi_pack.vobu_sri.next_vobu & 0x7fffffff);
    } else {
	*next = sector + blocks + 1;
    }

    return blocks;
}

static int
dvd_doread(dvd_t *d)
{
    int l = 0, r;

    switch(d->state){
    case CELL_START:
	if(d->next_cell >= d->pgc->nr_of_cells)
	    return -1;
	d->cell = d->next_cell;
/* 	fprintf(stderr, "DVD: entering cell %i\n", d->cell); */
	d->next_cell = next_cell(d->pgc, d->cell, d->angle);
	d->npack = d->pgc->cell_playback[d->cell].first_sector;
	d->state = CELL_LOOP;

    case CELL_LOOP:
	d->pack = d->npack;
	l = read_nav(d->file, d->pack, &d->npack);
	if(l < 0)
	    return -1;
/* 	fprintf(stderr, "DVD: cell %i, %i blocks @%i\n", d->cell, l, d->pack); */
	d->blocks = l;
	d->pack++;
	d->state = BLOCK_LOOP;

    case BLOCK_LOOP:
	r = min(d->blocks, buf_blocks);
/* 	    fprintf(stderr, "DVD: reading %i blocks @%i -> %p\n", */
/* 		    r, d->pack, d->buf); */
	l = DVDReadBlocks(d->file, d->pack, r, d->buf);
	if(l != r){
	    fprintf(stderr, "DVD: error reading %i blocks @%i\n",
		    r, d->pack);
	    return -1;
	}
	d->bbytes = l * DVD_VIDEO_LB_LEN;
	d->bpos = 0;
	d->blocks -= l;

	if(!d->blocks){
	    if(d->pack < d->pgc->cell_playback[d->cell].last_sector){
		d->state = CELL_LOOP;
	    } else {
		d->state = CELL_START;
	    }
	} else {
	    d->pack += l;
	}
    }

    return 0;
}

static int
dvd_read(void *buf, size_t size, size_t count, url_t *u)
{
    dvd_t *d = u->private;
    size_t rbytes = size * count;
    size_t bytes = rbytes;

    while(bytes){
	int bb = min(bytes, d->bbytes - d->bpos);

/* 	fprintf(stderr, "DVD: reading %i bytes @%lli\n", bb, d->pos); */
	memcpy(buf, d->buf + d->bpos, bb);
	d->bpos += bb;
	d->pos += bb;
	bytes -= bb;

	if(d->bpos == d->bbytes){
	    if(dvd_doread(d)){
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
	uint64_t block = np / DVD_VIDEO_LB_LEN + d->start_sector;
	int i;

	for(i = 0; i < d->vobu_map->last_byte / 4; i++){
	    if(d->vobu_map->vobu_start_sectors[i] - i > block)
		break;
	}

	if(!i)
	    return -1;

	d->npack = d->vobu_map->vobu_start_sectors[i-1];
	d->state = CELL_LOOP;
	np = (d->npack - d->start_sector) * DVD_VIDEO_LB_LEN;
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
    ifoClose(d->vts);
    ifoClose(d->ifo);
    DVDClose(d->dvd);
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
    int chapter = 0;
    char *device = url_dvd_conf_device;
    dvd_reader_t *dvd;
    dvd_file_t *file;
    ifo_handle_t *ifo, *vts;
    vts_ptt_srpt_t *vts_ptt_srpt;
    tt_srpt_t *ttsrpt;
    pgc_t *pgc;
    int pgc_id, start_cell;
    int ttn, pgn;
    char *p, *tmp;
    dvd_t *d;
    url_t *u;

    if(strcmp(mode, "r"))
	return NULL;

    if(!strncmp(url, "dvd:", 4))
	url += 4;

    url = strdupa(url);

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

    if(!(dvd = DVDOpen(device))){
	fprintf(stderr, "DVD: can't open %s\n", device);
	return NULL;
    }

    if(!(ifo = ifoOpen(dvd, 0))){
	fprintf(stderr, "DVD: can't get DVD info\n");
	DVDClose(dvd);
	return NULL;
    }

    ttsrpt = ifo->tt_srpt;
    fprintf(stderr, "DVD: %i titles.\n", ttsrpt->nr_of_srpts);

    if(title < 0 || title >= ttsrpt->nr_of_srpts){
	fprintf(stderr, "DVD: invalid title %i.\n", title);
	ifoClose(ifo);
	DVDClose(dvd);
	return NULL;
    }

    fprintf(stderr, "DVD: %i chapters.\n", ttsrpt->title[title].nr_of_ptts);
    fprintf(stderr, "DVD: %i angles.\n", ttsrpt->title[title].nr_of_angles);

    if(chapter < 0 || chapter >= ttsrpt->title[title].nr_of_ptts){
	fprintf(stderr, "DVD: invalid chapter %i\n", chapter);
	ifoClose(ifo);
	DVDClose(dvd);
	return NULL;
    }

    if(angle < 0 || angle >= ttsrpt->title[title].nr_of_angles){
	fprintf(stderr, "DVD: invalid angle %i\n", angle);
	ifoClose(ifo);
	DVDClose(dvd);
	return NULL;
    }

    if(!(vts = ifoOpen(dvd, ttsrpt->title[title].title_set_nr))){
	fprintf(stderr, "DVD: can't open title info.\n");
	ifoClose(ifo);
	DVDClose(dvd);
	return NULL;
    }

    ttn = ttsrpt->title[title].vts_ttn;
    vts_ptt_srpt = vts->vts_ptt_srpt;
    pgc_id = vts_ptt_srpt->title[ttn - 1].ptt[chapter].pgcn;
    pgn = vts_ptt_srpt->title[ttn - 1].ptt[chapter].pgn;
    pgc = vts->vts_pgcit->pgci_srp[pgc_id - 1].pgc;
    start_cell = pgc->program_map[pgn - 1] - 1;

    if(!(file = DVDOpenFile(dvd, ttsrpt->title[title].title_set_nr,
			    DVD_READ_TITLE_VOBS))){
	fprintf(stderr, "DVD: can't open title %i\n", title);
	ifoClose(vts);
	ifoClose(ifo);
	DVDClose(dvd);
	return NULL;
    }

    d = calloc(1, sizeof(*d));
    d->dvd = dvd;
    d->file = file;
    d->ifo = ifo;
    d->vts = vts;
    d->vobu_map = vts->vts_vobu_admap;

    d->start_cell = d->next_cell = start_cell;
    d->pgc = pgc;
    d->start_sector = pgc->cell_playback[start_cell].first_sector;
    d->angle = angle;
    d->state = CELL_START;

    d->bufsize = url_dvd_conf_buffer * DVD_VIDEO_LB_LEN;
    d->buf = malloc(d->bufsize);

    u = tcallocz(sizeof(*u));
    u->read = dvd_read;
    u->seek = dvd_seek;
    u->tell = dvd_tell;
    u->close = dvd_close;
    u->private = d;

    u->size = (uint64_t) DVDFileSize(d->file) * DVD_VIDEO_LB_LEN;

    return u;
}
