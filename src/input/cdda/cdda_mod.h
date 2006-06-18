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

#ifndef CDDA_MOD_H
#define CDDA_MOD_H

#define PARANOIA_NUM_CODES 13

typedef struct {
    cdrom_drive *drive;
    cdrom_paranoia *cdprn;
    u_char *header;
    int header_length;
    int64_t pos;
    int64_t data_length;
    int first_sector;
    int last_sector;
    int current_sector;
    char *buffer;
    int bufsize;
    int buffer_pos;
    int prn_stats[PARANOIA_NUM_CODES];
    char track[4];
} cd_data_t;

extern int cdda_freedb(url_t *u, cd_data_t *, int track, char *options);

#endif
