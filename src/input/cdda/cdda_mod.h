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
