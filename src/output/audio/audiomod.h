/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#ifndef _AUDIOMOD_H
#define _AUDIOMOD_H

typedef void (*sndconv_t)(void *dst, void *src, int samples, int channels);

extern sndconv_t audio_conv(char *in, char *out);
extern char **audio_all_conv(char *in);

#endif
