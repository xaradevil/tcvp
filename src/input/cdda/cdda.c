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
#include <stdio.h>
#include <cdda_tc2.h>
#include <cdda_interface.h>
#include <cdda_paranoia.h>
#include <tcstring.h>
#include <tcalloc.h>
#include <cdda_mod.h>

#define buffer_size tcvp_input_cdda_conf_buffer
#define device tcvp_input_cdda_conf_device
#define paranoia tcvp_input_cdda_conf_paranoia
#define max_retries 64

#define min(a, b) ((a)<(b)?(a):(b))

static char *stat_names[] = {
    "read",
    "verify",
    "fixup_edge",
    "fixup_atom",
    "scratch",
    "repair",
    "skip",
    "drift",
    "backoff",
    "overlap",
    "fixup_dropped",
    "fixup_duped",
    "readerr"
};

static int
print_paranoia_stats(cd_data_t *cdt)
{
    int i;

    for(i = 0; i < 13; i++)
	fprintf(stderr, "%5i %s\n", cdt->prn_stats[i], stat_names[i]);

    return 0;
}

static int
fill_buffer(cd_data_t *cdt)
{
    int nsect, bpos = 0;

    nsect = cdt->bufsize / CD_FRAMESIZE_RAW;
    while(nsect){
	if(cdt->cdprn){
	    void cdp_cb(long foo, int stat){
		if(stat >= 0 && stat < sizeof(cdt->prn_stats))
		    cdt->prn_stats[stat]++;
	    }
	    int16_t *sect = paranoia_read_limited(cdt->cdprn, cdp_cb,
						  max_retries);
	    char *err = cdda_errors(cdt->drive);
	    if(err){
		fprintf(stderr, "CDDA: error reading sector %i: %s\n",
			cdt->current_sector, err);
		return -1;
	    } else {
		memcpy(cdt->buffer + bpos, sect, CD_FRAMESIZE_RAW);
		nsect--;
		cdt->current_sector++;
		bpos += CD_FRAMESIZE_RAW;
	    }
	} else {
	    int s = cdda_read(cdt->drive, cdt->buffer + bpos,
			      cdt->current_sector, nsect);
	    char *err = cdda_errors(cdt->drive);
	    if(s < 0 || err){
		fprintf(stderr, "CDDA: error reading sector %i: %s\n",
			cdt->current_sector, err);
		return -1;
	    } else {
		nsect -= s;
		cdt->current_sector += s;
		bpos += s * CD_FRAMESIZE_RAW;
	    }
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
	if(cdt->cdprn){
	    paranoia_seek(cdt->cdprn, cdt->current_sector, SEEK_SET);
	}
    }

    return 0;
}

extern int
cd_close(url_t *u)
{
    cd_data_t *cdt = u->private;

    if(cdt->cdprn){
	paranoia_free(cdt->cdprn);
	if(tcvp_input_cdda_conf_paranoia_stats)
	    print_paranoia_stats(cdt);
    }
    cdda_close(cdt->drive);
    free(cdt->buffer);
    free(cdt->header);
    free(cdt);
    tcfree(u);
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

    u = tcallocz(sizeof(*u));
    u->read = cd_read;
    u->write = NULL;
    u->seek = cd_seek;
    u->tell = cd_tell;
    u->close = cd_close;

    cdt = calloc(sizeof(cd_data_t), 1);

    cdt->track = track;
    cdt->drive = cdda_identify(device, CDDA_MESSAGE_FORGETIT, NULL);
    if(!cdt->drive){
	free(cdt);
	free(u);
	return NULL;
    }

    cdda_verbose_set(cdt->drive, CDDA_MESSAGE_LOGIT, CDDA_MESSAGE_FORGETIT);

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

    if(paranoia){
	cdt->cdprn = paranoia_init(cdt->drive);
	paranoia_modeset(cdt->cdprn,
			 PARANOIA_MODE_FULL - PARANOIA_MODE_NEVERSKIP);
	paranoia_seek(cdt->cdprn, cdt->first_sector, SEEK_SET);
    }

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

    if(tcvp_input_cdda_conf_cddb)
	cdda_freedb(u, track);

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

    u = tcallocz(sizeof(*u));
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
