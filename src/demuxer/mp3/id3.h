/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#ifndef _MP3_H
#define _MP3_H

#include <tcvp_types.h>
#include <mp3_tc2.h>

extern int id3v2_tag(url_t *, muxed_stream_t *ms);
extern int id3v2_write_tag(url_t *u, muxed_stream_t *ms);

extern int id3v1_tag(url_t *, muxed_stream_t *ms);

#endif
