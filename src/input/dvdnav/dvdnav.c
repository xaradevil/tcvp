/**
    Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

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
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcendian.h>
#include <stdint.h>
#include <tcalloc.h>
#include <dvdnav/dvdnav.h>
#include <dvdnav/ifo_read.h>
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
    int vts;
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
	int32_t event = DVDNAV_BLOCK_OK, len;
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
			tc2_print("DVD", TC2_PRINT_DEBUG, "still\n");
			flush(d, 0);
			d->still = 1;
			d->pci = dvdnav_get_current_nav_pci(d->dvd);
			write_pes(d, &t, sizeof(t));
		    } else {
			usleep(100000);
		    }
		    break;
		case DVDNAV_WAIT:
		    tc2_print("DVD", TC2_PRINT_DEBUG, "wait\n");
/* 		    flush(d, 0); */
		    dvdnav_wait_skip(d->dvd);
		    break;
		case DVDNAV_STOP:
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
		    tc2_print("DVD", TC2_PRINT_DEBUG, "hop_channel\n");
		    flush(d, 1);
		    break;
		case DVDNAV_VTS_CHANGE: {
		    dvdnav_vts_change_event_t *vce =
			(dvdnav_vts_change_event_t *) d->buf;
		    tc2_print("DVD", TC2_PRINT_DEBUG, "vts change %i\n", vce->new_vtsN);
		    if(!tcvp_input_dvdnav_conf_enable_menus && d->vts){
			d->end = 1;
			return -1;
		    }
		    d->vts = vce->new_vtsN;
/* 		    flush(d, 0); */
/* 		    set_ptsoffset(d, 0); */
/* 		    d->pts = 0; */
		    break;
		}
		case DVDNAV_AUDIO_STREAM_CHANGE: {
		    dvdnav_audio_stream_change_event_t *asc =
			(dvdnav_audio_stream_change_event_t *) d->buf;
		    dvd_audio_id_t dai;
		    tc2_print("DVD", TC2_PRINT_DEBUG, "audio stream change %x %x\n",
			    asc->physical, asc->logical);
		    dai.type = DVD_AUDIO_ID;
		    dai.id = asc->logical & 0x80? asc->logical: 0x80;
		    write_pes(d, &dai, sizeof(dai));
		    break;
		}
		case DVDNAV_CELL_CHANGE:
		    tc2_print("DVD", TC2_PRINT_DEBUG, "cell change\n");
		    break;
		default:
		    tc2_print("DVD", TC2_PRINT_DEBUG, "event %i\n", event);
		    break;
		}
	    }
	}

	bb = min(bytes, d->bbytes - d->bpos);
/* 	tc2_print("DVD", TC2_PRINT_DEBUG, "reading %i bytes @%lli\n", bb, d->pos); */
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
	    tc2_print("DVD", TC2_PRINT_ERROR,
		      "error seeking to sector %lli\n", sector);
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

static void
dvd_free_info(void *p)
{
    dvd_functions_t *df = p;
    free(df->streams);
    free(df->index);
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
    dvd_reader_t *dvdr = NULL;
    ifo_handle_t *ifo = NULL;
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
	tc2_print("DVD", TC2_PRINT_ERROR, "can't open %s\n", device);
	goto err;
    }

    dvdnav_set_PGC_positioning_flag(dvd, 1);

    dvdnav_menu_language_select(dvd, url_dvd_conf_language);
    dvdnav_audio_language_select(dvd, url_dvd_conf_language);
    dvdnav_spu_language_select(dvd, url_dvd_conf_language);

    if(title > 0){
	dvdnav_get_number_of_titles(dvd, &titles);
	tc2_print("DVD", TC2_PRINT_VERBOSE, "%i titles.\n", titles);
	if(title < 1 || title > titles){
	    tc2_print("DVD", TC2_PRINT_ERROR, "invalid title %i.\n", title);
	    goto err;
	}

	if(dvdnav_get_number_of_parts(dvd, title, &chapters) !=
	   DVDNAV_STATUS_OK){
	    tc2_print("DVD", TC2_PRINT_ERROR, "error getting number of chapters.\n");
	    goto err;
	}

	tc2_print("DVD", TC2_PRINT_VERBOSE, "%i chapters.\n", chapters);
	if(chapter < 1 || chapter > chapters){
	    tc2_print("DVD", TC2_PRINT_ERROR, "invalid chapter %i.\n", chapter);
	    goto err;
	}

	if(dvdnav_part_play(dvd, title, chapter) != DVDNAV_STATUS_OK){
	    tc2_print("DVD", TC2_PRINT_ERROR, "error playing title %i, chapter %i\n",
		    title, chapter);
	    goto err;
	}

	dvdnav_get_angle_info(dvd, &ca, &angles);
	tc2_print("DVD", TC2_PRINT_VERBOSE, "%i angles.\n", angles);
	if(angle < 1 || angle > angles){
	    tc2_print("DVD", TC2_PRINT_ERROR, "invalid angle %i.\n", angle);
	    goto err;
	}

	if(dvdnav_angle_change(dvd, angle) != DVDNAV_STATUS_OK){
	    tc2_print("DVD", TC2_PRINT_ERROR, "error setting angle %i\n", angle);
	    goto err;
	}

	if(dvdnav_get_position_in_title(dvd, &pos, &len) != DVDNAV_STATUS_OK){
	    tc2_print("DVD", TC2_PRINT_ERROR, "error getting size.\n");
	    goto err;
	}
    }

    df = calloc(1, sizeof(*df));
    df->button = dvd_button;
    df->enable = dvd_enable;
    df->menu = dvd_menu;

    dvdr = DVDOpen(device);
    ifo = ifoOpen(dvdr, title);
    if(ifo){
	pgcit_t *pgcit = ifo->vts_pgcit;
	vtsi_mat_t *vtsi = ifo->vtsi_mat;
	video_attr_t *vattr = &vtsi->vtsm_video_attr;
	pgc_t *pgc = pgcit->pgci_srp[0].pgc;
	vts_tmapt_t *tmapt = ifo->vts_tmapt;
	vts_tmap_t *tmap;
	int i;

	for(i = 0; i < 16; i++)
	    df->spu_palette[i] = pgc->palette[i];

	df->n_streams =
	    1 + vtsi->nr_of_vts_audio_streams + vtsi->nr_of_vts_subp_streams;
	df->streams = calloc(df->n_streams, sizeof(*df->streams));

	/* video stream */
	df->streams[0].stream_type = STREAM_TYPE_VIDEO;
	df->streams[0].common.index = 0xe0;
	df->streams[0].common.codec =
	    vattr->mpeg_version? "video/mpeg": "video/mpeg2";

	if(vattr->display_aspect_ratio == 0){
	    df->streams[0].video.aspect.num = 4;
	    df->streams[0].video.aspect.num = 3;
	} else if(vattr->display_aspect_ratio == 3){
	    df->streams[0].video.aspect.num = 16;
	    df->streams[0].video.aspect.num = 9;
	}

	df->streams[0].video.height = vattr->video_format? 576: 480;
	switch(vattr->picture_size){
	case 0:
	    df->streams[0].video.width = 720;
	    break;
	case 1:
	    df->streams[0].video.width = 704;
	    break;
	case 3:
	    df->streams[0].video.height /= 2;
	case 2:
	    df->streams[0].video.width = 352;
	    break;
	}

	/* audio streams */
	for(i = 0; i < vtsi->nr_of_vts_audio_streams; i++){
	    audio_attr_t *ast = vtsi->vts_audio_attr + i;
	    stream_t *st = df->streams + i + 1;
	    st->stream_type = STREAM_TYPE_AUDIO;
	    st->audio.channels = ast->channels + 1;
	    st->audio.sample_rate = ast->sample_frequency? 96000: 48000;
	    st->audio.language[0] = ast->lang_code >> 8;
	    st->audio.language[1] = ast->lang_code & 0xff;

	    switch(ast->audio_format){
	    case 0:
		st->common.index = 0x80 + i;
		st->common.codec = "audio/ac3";
		break;
	    case 2:
	    case 3:
		st->common.index = 0xc0 + i;
		st->common.codec = "audio/mpeg";
		break;
	    case 4:
		st->common.index = 0xa0 + i;
		st->common.codec = "audio/pcm-s16be";
		st->common.bit_rate =
		    st->audio.channels * st->audio.sample_rate * 16;
		break;
	    case 6:
		st->common.index = 0x88 + i;
		st->common.codec = "audio/dts";
		break;
	    }
	}

	/* subtitle streams */
	for(i = 0; i < vtsi->nr_of_vts_subp_streams; i++){
	    subp_attr_t *sst = vtsi->vts_subp_attr + i;
	    stream_t *st = df->streams + i + 1 + vtsi->nr_of_vts_audio_streams;
	    st->stream_type = STREAM_TYPE_SUBTITLE;
	    st->common.index = 0x20 + i;
	    st->common.codec = "subtitle/dvd";
	    st->common.codec_data = df->spu_palette;
	    st->common.codec_data_size = sizeof(df->spu_palette);
	    st->subtitle.language[0] = sst->lang_code >> 8;
	    st->subtitle.language[1] = sst->lang_code & 0xff;
	}

	tmap = tmapt->tmap;
	df->index_unit = tmap->tmu * 27000000ULL;
	df->index_size = tmap->nr_of_entries;
	df->index = calloc(df->index_size, sizeof(*df->index));
	for(i = 0; i < tmap->nr_of_entries; i++){
	    df->index[i] = (tmap->map_ent[i] & 0x7fffffff) * DVD_SECTOR_SIZE;
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

    tcattr_set(u, "dvd", df, NULL, dvd_free_info);

  out:
    if(ifo)
	ifoClose(ifo);
    if(dvdr)
	DVDClose(dvdr);
    free(url);
    return u;

  err:
    if(dvd)
	dvdnav_close(dvd);
    u = NULL;
    goto out;
}
