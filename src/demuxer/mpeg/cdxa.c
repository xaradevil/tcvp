/**
    Copyright (C) 2004  Michael Ahlberg, Måns Rullgård

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

#include <string.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

#define SECTOR_SIZE 2352
#define SECTOR_DATA 2324
#define SECTOR_PAD  (SECTOR_SIZE - SECTOR_DATA)

struct cdxa {
    url_t *u;
    uint64_t data_start;
    int sector_pos;
};

#define min(a, b) ((a)<(b)?(a):(b))

static int
cdxa_read(void *buf, size_t size, size_t count, url_t *u)
{
    struct cdxa *cdxa = u->private;
    size_t bytes = size * count, rb = bytes;

    while(rb){
        size_t b = min(rb, SECTOR_DATA - cdxa->sector_pos);
        ssize_t r = cdxa->u->read(buf, 1, b, cdxa->u);
        if(r <= 0)
            break;
        rb -= r;
        cdxa->sector_pos += r;
        if(cdxa->sector_pos == SECTOR_DATA){
            char p[SECTOR_PAD];
            if(cdxa->u->read(p, 1, SECTOR_PAD, cdxa->u) < SECTOR_PAD)
                break;
            cdxa->sector_pos = 0;
        }
    }

    return (bytes - rb) / size;
}

static int
cdxa_seek(url_t *u, int64_t offset, int how)
{
    struct cdxa *cdxa = u->private;
    int64_t pos;
    uint64_t sector, xaoffset;
    int sector_pos;

    switch(how){
    case SEEK_SET:
        pos = offset;
        break;
    case SEEK_CUR:
        if(cdxa->u->tell)
            pos = cdxa->u->tell(cdxa->u) + offset;
        else
            return -1;
        break;
    case SEEK_END:
        pos = u->size + offset;
        break;
    default:
        return -1;
    }

    if(pos > u->size)
        return -1;

    sector = pos / SECTOR_DATA;
    sector_pos = pos - sector * SECTOR_DATA;
    xaoffset = cdxa->data_start + sector * SECTOR_SIZE + sector_pos;

    if(cdxa->u->seek(cdxa->u, xaoffset, SEEK_SET))
        return -1;

    cdxa->sector_pos = sector_pos;

    return 0;
}

static uint64_t
cdxa_tell(url_t *u)
{
    struct cdxa *cdxa = u->private;
    uint64_t pos, sector;
    int sector_pos;

    if(!cdxa->u->tell)
        return 0;

    pos = cdxa->u->tell(cdxa->u);
    pos -= cdxa->data_start;
    sector = pos / SECTOR_SIZE;
    sector_pos = pos - sector * SECTOR_SIZE;

    return sector * SECTOR_DATA + sector_pos;
}

static int
cdxa_close(url_t *u)
{
    struct cdxa *cdxa = u->private;
    int r = cdxa->u->close(cdxa->u);
    cdxa->u = NULL;
    tcfree(u);
    return r;
}

static void
cdxa_free(void *p)
{
    url_t *u = p;
    struct cdxa *cdxa = u->private;
    if(cdxa->u)
        tcfree(cdxa->u);
    free(cdxa);
}

extern muxed_stream_t *
cdxa_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *t)
{
    struct cdxa *cdxa;
    char buf[28];
    int32_t s;
    url_t *cu;

    if(u->read(buf, 1, 8, u) < 8)
        return NULL;
    if(strncmp(buf, "RIFF", 4)){
        tc2_print("CDXA", TC2_PRINT_ERROR, "no RIFF header\n");
        return NULL;
    }

    if(u->read(buf, 1, 4, u) < 4)
        return NULL;
    if(strncmp(buf, "CDXA", 4)){
        tc2_print("CDXA", TC2_PRINT_ERROR, "no CDXA header\n");
        return NULL;
    }

    if(u->read(buf, 1, 4, u) < 4)
        return NULL;
    if(strncmp(buf, "fmt ", 4)){
        tc2_print("CDXA", TC2_PRINT_ERROR, "no 'fmt ' tag\n");
        return NULL;
    }

    if(url_get32l(u, &s))
        return NULL;

    while(s > 0){
        int r = u->read(buf, 1, s <= 16? s: 16, u);
        if(r <= 0)
            return NULL;
        s -= r;
    }

    if(u->read(buf, 1, 4, u) < 4)
        return NULL;
    if(strncmp(buf, "data", 4))
        return NULL;

    if(u->read(buf, 1, 28, u) < 28)
        return NULL;

    cdxa = calloc(1, sizeof(*cdxa));
    cdxa->u = tcref(u);
    cdxa->data_start = u->tell(u);

    tc2_print("CDXA", TC2_PRINT_DEBUG, "data @ %llx\n", cdxa->data_start);

    cu = tcallocdz(sizeof(*cu), NULL, cdxa_free);
    cu->size = (u->size - cdxa->data_start) / SECTOR_SIZE * SECTOR_DATA;
    cu->flags = u->flags;
    cu->read = cdxa_read;
    cu->seek = cdxa_seek;
    cu->tell = cdxa_tell;
    cu->close = cdxa_close;
    cu->private = cdxa;

    return mpeg_open(name, cu, cs, t);
}
