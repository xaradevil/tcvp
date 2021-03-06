/**
    Copyright (C) 2003-2006  Michael Ahlberg, Måns Rullgård

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
#include <tcvp_types.h>
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
        tc2_print("CDDA", TC2_PRINT_INFO,
                  "%5i %s\n", cdt->prn_stats[i], stat_names[i]);

    return 0;
}

static __thread int *paranoia_stats;

static void
paranoia_cb(long foo, int stat)
{
    if(paranoia_stats && stat >= 0 && stat < PARANOIA_NUM_CODES)
        paranoia_stats[stat]++;
}

static int
fill_buffer(cd_data_t *cdt)
{
    int nsect, bpos = 0;

    nsect = cdt->bufsize / CD_FRAMESIZE_RAW;
    while(nsect){
        if(cdt->cdprn){
            int16_t *sect;
            char *err;

            paranoia_stats = cdt->prn_stats;
            sect = paranoia_read_limited(cdt->cdprn, paranoia_cb, max_retries);
            paranoia_stats = NULL;

            err = cdda_errors(cdt->drive);

            if(err){
                tc2_print("CDDA", TC2_PRINT_ERROR, "error reading sector %i: %s\n",
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
            if(err)
                tc2_print("CDDA", TC2_PRINT_ERROR, "error reading sector %i: %s\n",
                        cdt->current_sector, err);
            if(s < 0){
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
        pos = cdt->data_length - offset;
    }

    if(pos < 0)
        return -1;

    if(pos > cdt->data_length)
        return -1;

    if(cdt->pos != pos) {
        cdt->buffer_pos = cdt->bufsize;
        cdt->pos = pos;
        cdt->current_sector = cdt->first_sector + cdt->pos / CD_FRAMESIZE_RAW;
        if(cdt->cdprn){
            paranoia_seek(cdt->cdprn, cdt->current_sector, SEEK_SET);
        }
    }

    return 0;
}

extern int
cd_close(url_t *u)
{
    tcfree(u);
    return 0;
}

static void
cdda_free(void *p)
{
    url_t *u = p;
    cd_data_t *cdt = u->private;

    if(cdt->cdprn){
        paranoia_free(cdt->cdprn);
        if(tcvp_input_cdda_conf_paranoia_stats)
            print_paranoia_stats(cdt);
    }
    cdda_close(cdt->drive);
    if(cdt->buffer)
        free(cdt->buffer);
    free(cdt->header);
    free(cdt);
}

static url_t *
track_open(char *url, char *mode, char *options)
{
    stream_t s;
    char *trk;
    url_t *u, *vu;
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

    cdt = calloc(sizeof(cd_data_t), 1);

    cdt->drive = cdda_identify(device, CDDA_MESSAGE_FORGETIT, NULL);
    if(!cdt->drive){
        free(cdt);
        return NULL;
    }

    cdda_verbose_set(cdt->drive, CDDA_MESSAGE_LOGIT, CDDA_MESSAGE_FORGETIT);

    if(cdda_open(cdt->drive) != 0) {
        free(cdt);
        return NULL;
    }

    if(track > cdda_tracks(cdt->drive) ||
       !cdda_track_audiop(cdt->drive, track)) {
        free(cdt);
        return NULL;
    }

    cdt->bufsize = buffer_size * CD_FRAMESIZE_RAW;
    cdt->buffer = malloc(cdt->bufsize);
    cdt->buffer_pos = cdt->bufsize;

    cdt->first_sector = cdda_track_firstsector(cdt->drive, track);
    cdt->last_sector = cdda_track_lastsector(cdt->drive, track);
    cdt->current_sector = cdt->first_sector;

    if(paranoia){
        cdt->cdprn = paranoia_init(cdt->drive);
        paranoia_modeset(cdt->cdprn,
                         PARANOIA_MODE_FULL - PARANOIA_MODE_NEVERSKIP);
        paranoia_seek(cdt->cdprn, cdt->first_sector, SEEK_SET);
    }

    cdt->data_length =
        (cdt->last_sector - cdt->first_sector) * CD_FRAMESIZE_RAW;

    memset(&s, 0, sizeof(s));
    s.stream_type = STREAM_TYPE_AUDIO;
    s.audio.codec = "audio/pcm-s16le";
    s.audio.bit_rate = 16 * 2 * 44100;
    s.audio.sample_rate = 44100;
    s.audio.channels = 2;
    s.audio.samples = cdt->data_length / 4;
    s.audio.block_align = 4;
    s.audio.sample_size = 16;

    cdt->header = mux_wav_header(&s, &cdt->header_length);

    u = tcallocdz(sizeof(*u), NULL, cdda_free);
    u->read = cd_read;
    u->write = NULL;
    u->seek = cd_seek;
    u->tell = cd_tell;
    u->close = cd_close;

    u->private = cdt;
    u->size = cdt->data_length;

    vu = url_vheader_new(u, cdt->header, cdt->header_length);

    if(tcvp_input_cdda_conf_cddb)
        cdda_freedb(vu, cdt, track, options);

    snprintf(cdt->track, sizeof(cdt->track), "%i", track);
    tcattr_set(vu, "track", cdt->track, NULL, NULL);

    return vu;
}

static url_t *
list_open(char *url, char *mode, char *options)
{
    cd_data_t *cdt;
    int tracks;
    url_t *u;
    u_char *p;
    int i;
    int optlen = 0;

    u = tcallocz(sizeof(*u));
    u->close = cd_close;

    cdt = calloc(sizeof(cd_data_t), 1);

    cdt->drive = cdda_identify(device, CDDA_MESSAGE_FORGETIT, NULL);
    if(!cdt->drive)
        goto err;

    cdda_verbose_set(cdt->drive, CDDA_MESSAGE_PRINTIT, CDDA_MESSAGE_FORGETIT);

    if(cdda_open(cdt->drive) != 0) {
        goto err;
    }

    cdt->pos = 0;
    cdt->data_length = 0;

    tracks = cdda_tracks(cdt->drive);

    if(options) {
        optlen = strlen(options) + 1;
    }

    cdt->header = malloc(tracks * (16 + optlen));
    p = cdt->header;

    for(i = 1; i <= tracks; i++) {
        if(cdda_track_audiop(cdt->drive, i)) {
            if(!options)
                p += sprintf(p, "cdda:/%d.wav\n", i);
            else
                p += sprintf(p, "cdda:/%d.wav?%s\n", i, options);
        }
    }

    cdt->header_length = p - cdt->header;

    u->private = cdt;
    u->size = 0;

    return url_vheader_new(u, cdt->header, cdt->header_length);

err:
    free(cdt);
    tcfree(u);
    return NULL;
}


extern url_t *
cd_open(char *url, char *mode)
{
    char *url_tmp = strdup(url);
    url_t *u = NULL;
    char *s, *o;

    if(!url_tmp)
        return NULL;

    o = strrchr(url_tmp, '?');
    if(o)
        *o++ = 0;

    s = strrchr(url_tmp, '.');
    if(s){
        if(!strcmp(s, ".wav")){
            u = track_open(url_tmp, mode, o);
        } else if(!strcmp(s, ".m3u")){
            u = list_open(url_tmp, mode, o);
        }
    }

    if(!u)
        tc2_print("CDDA", TC2_PRINT_ERROR, "unsupported URL: %s\n", url);

    free(url_tmp);
    return u;
}
