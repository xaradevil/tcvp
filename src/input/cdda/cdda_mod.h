/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#ifndef _CDDA_MOD_H
#define _CDDA_MOD_H

typedef struct {
    cdrom_drive *drive;
    cdrom_paranoia *cdprn;
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
    int prn_stats[13];
} cd_data_t;

extern int cdda_freedb(url_t *u, int track);

#endif
