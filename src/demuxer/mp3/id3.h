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

#ifndef _MP3_H
#define _MP3_H

#include <tcvp_types.h>
#include <mp3_tc2.h>

extern int id3v2_tag(url_t *, muxed_stream_t *ms);
extern int id3v2_write_tag(url_t *u, muxed_stream_t *ms);

extern int id3v1_tag(url_t *, muxed_stream_t *ms);

#endif
