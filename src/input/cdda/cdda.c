/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

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
#include <cdda_tc2.h>
#include <cdda_interface.h>
#include <tcstring.h>

#define buffer_size tcvp_input_cdda_conf_buffer
#define device tcvp_input_cdda_conf_device

#define min(a, b) ((a)<(b)?(a):(b))

typedef struct {
    cdrom_drive *drive;
    void *header;
    int header_length;
    int64_t pos;
    int64_t data_length;
    int track;
    int first_sector;
    int last_sector;
    int current_sector;
    char *buffer;
    int bufsize;
    int buffer_pos;
} cd_data_t;

static int
fill_buffer(cd_data_t *cdt)
{
    int nsect, bpos = 0;
    int err;

    nsect = cdt->bufsize / CD_FRAMESIZE_RAW;
    while(nsect){
	err = cdda_read(cdt->drive, cdt->buffer + bpos,
			cdt->current_sector, 1);
	if(err < TR_EREAD){
	    nsect--;
	    cdt->current_sector++;
	    bpos += CD_FRAMESIZE_RAW;
	} else {
	    fprintf(stderr, "CDDA: read error %i: %s\n",
		    err, strerror_tr[err]);
	    return -1;
	}
    }

    cdt->buffer_pos = 0;

    return 0;
}

extern int
cd_read(void *data, size_t size, size_t count, url_t *u)
{
    cd_data_t *cdt = u->private;
    int remain = size*count;
    int used = 0;

    if(cdt->pos >= u->size)
	return -1;

    if(cdt->pos < cdt->header_length) {
	int i = size*count;
	if(cdt->pos + i > cdt->header_length) {
	    i = cdt->header_length - cdt->pos;
	}
	memcpy(data, cdt->header+cdt->pos, i);
	remain -= i;
	used += i;
    }

    while(remain > 0) {
	if(cdt->pos + used < u->size && cdt->data_length > 0){
	    int n;

	    if(cdt->buffer_pos == cdt->bufsize)
		if(fill_buffer(cdt))
		    return -1;

	    n = min(remain, cdt->bufsize - cdt->buffer_pos);
	    memcpy(data + used, cdt->buffer + cdt->buffer_pos, n);
	    cdt->buffer_pos += n;
	    used += n;
	    remain -= n;
	} else {
	    break;
	}
    }

    cdt->pos += used;
    return used / size;
}

extern uint64_t
cd_tell(url_t *u)
{
    cd_data_t *cdt = u->private;
    return cdt->pos;
}

extern int
cd_seek(url_t *u, int64_t offset, int how)
{
    cd_data_t *cdt = u->private;
    int64_t pos = 0;

    if(how == SEEK_SET) {
	pos = offset;
    } else if(how == SEEK_CUR) {
	pos = cdt->pos + offset;
    } else if(how == SEEK_END) {
	pos = cdt->data_length + cdt->header_length - offset;
    }
    if(pos < 0) {
	pos = 0;
    }
    if(pos > cdt->header_length + cdt->data_length) {
	pos = cdt->header_length + cdt->data_length;
    }

    if(cdt->pos != pos) {
	cdt->buffer_pos = cdt->bufsize;
	cdt->pos = pos;
	cdt->current_sector = cdt->first_sector +
	    (cdt->pos - cdt->header_length) / CD_FRAMESIZE_RAW;
    }

    return 0;
}

extern int
cd_close(url_t *u)
{
    cd_data_t *cdt = u->private;

    cdda_close(cdt->drive);
    free(cdt->buffer);
    free(cdt->header);
    free(cdt);
    free(u);
    return 0;
}

static url_t *
track_open(char *url, char *mode)
{
    char *trk;
    url_t *u;
    cd_data_t *cdt;
    int track;

    trk = strchr(url, ':');

    if(!trk)
	trk = url;
    else
	trk++;

    while(*trk == '/')
	trk++;

    track = strtol(trk, NULL, 0);
    if(track <= 0)
	return NULL;

    u = calloc(1, sizeof(url_t));
    u->read = cd_read;
    u->write = NULL;
    u->seek = cd_seek;
    u->tell = cd_tell;
    u->close = cd_close;

    cdt = calloc(sizeof(cd_data_t), 1);

    cdt->track = track;
    cdt->drive = cdda_identify(device, CDDA_MESSAGE_FORGETIT, NULL);
    cdda_verbose_set(cdt->drive, CDDA_MESSAGE_PRINTIT, CDDA_MESSAGE_FORGETIT);

    if(cdda_open(cdt->drive) != 0) {
	free(cdt);
	free(u);
	return NULL;
    }

    if(cdt->track > cdda_tracks(cdt->drive) || 
       cdda_track_audiop(cdt->drive, cdt->track) == 0) {
	free(cdt);
	free(u);
	return NULL;
    }

    cdt->bufsize = buffer_size * CD_FRAMESIZE_RAW;
    cdt->buffer = malloc(cdt->bufsize);
    cdt->buffer_pos = cdt->bufsize;

    cdt->first_sector = cdda_track_firstsector(cdt->drive, cdt->track);
    cdt->last_sector = cdda_track_lastsector(cdt->drive, cdt->track);

    cdt->header = malloc(44);
    cdt->header_length = 44;
    cdt->pos = 0;
    cdt->data_length = (cdt->last_sector - cdt->first_sector) *
	CD_FRAMESIZE_RAW;
    {
	uint32_t *i32 = cdt->header;
	uint16_t *i16 = cdt->header;
	uint8_t *i8 = cdt->header;

	memcpy(&i8[0], "RIFF", 4);
	i32[1] = cdt->data_length+44-8;
	memcpy(&i8[8], "WAVEfmt ", 8);
	i32[4] = 16;
	i16[10] = 1;
	i16[11] = 2;
	i32[6] = 44100;
	i32[7] = 44100*2*2;
	i16[16] = 4;
	i16[17] = 16;
	memcpy(&i8[36], "data", 4);
	i32[10] = cdt->data_length;
    }
    u->private = cdt;
    u->size = cdt->data_length + cdt->header_length;

    return u;
}

static url_t *
list_open(char *url, char *mode)
{
    cd_data_t *cdt;
    int tracks;
    url_t *u;
    char *p;
    int i;

    u = calloc(1, sizeof(url_t));
    u->read = cd_read;
    u->write = NULL;
    u->seek = cd_seek;
    u->tell = cd_tell;
    u->close = cd_close;

    cdt = calloc(sizeof(cd_data_t), 1);

    cdt->drive = cdda_identify(device, CDDA_MESSAGE_FORGETIT, NULL);
    cdda_verbose_set(cdt->drive, CDDA_MESSAGE_PRINTIT, CDDA_MESSAGE_FORGETIT);

    if(cdda_open(cdt->drive) != 0) {
	free(cdt);
	free(u);
	return NULL;
    }

    cdt->pos = 0;
    cdt->data_length = 0;

    tracks = cdda_tracks(cdt->drive);

    cdt->header = malloc(tracks * 16);
    p = cdt->header;

    for(i = 1; i <= tracks; i++) {
	if(cdda_track_audiop(cdt->drive, cdt->track) != 0) {
	    int l = sprintf(p, "cdda:/%d.wav\n", i);
	    p += l;
	    cdt->header_length += l;
	}
    }

    u->private = cdt;
    u->size = cdt->header_length;

    return u;
}


extern url_t *
cd_open(char *url, char *mode)
{
    char *s = strrchr(url, '.');

    if(s){
	if(!strcmp(s, ".wav")){
	    return track_open(url, mode);
	} else if(!strcmp(s, ".m3u")){
	    return list_open(url, mode);
	}
    }

    fprintf(stderr, "CDDA: unsupported URL: %s\n", url);
    return NULL;
}
