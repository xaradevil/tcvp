/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#ifndef _AVC_H
#define _AVC_H

#include <ffmpeg/avcodec.h>
#include <tcvp_types.h>

extern enum CodecID avc_codec_id(char *);
extern char *avc_codec_name(enum CodecID id);

#endif
