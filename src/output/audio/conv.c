/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tctypes.h>
#include <unistd.h>
#include <tcbyteswap.h>
#include <audio_tc2.h>
#include <audiomod.h>

static void
bs16(void *dst, void *src, int samples, int channels)
{
    int16_t *d = dst, *s = src;
    int i;

    for(i = 0; i < samples * channels; i++)
	d[i] = bswap_16(s[i]);
}

#define copy(ss)						\
static void							\
copy_##ss(void *dst, void *src, int samples, int channels)	\
{								\
    memcpy(dst, src, samples * channels * ss / 8);		\
}

copy(8)
copy(16)

static struct {
    char *in;
    char *out;
    sndconv_t conv;
} conv_table[] = {
    { "s16le", "s16be", bs16 },
    { "s16be", "s16le", bs16 },
    { "u16le", "u16be", bs16 },
    { "u16be", "u16le", bs16 },
    { "s16le", "s16le", copy_16 },
    { "s16be", "s16be", copy_16 },
    { "u16le", "u16le", copy_16 },
    { "u16be", "u16be", copy_16 },
    { "u8",    "u8",    copy_8 },
    { NULL, NULL, NULL }
};

extern sndconv_t
audio_conv(char *in, char *out)
{
    int i;

    for(i = 0; conv_table[i].in; i++){
	if(!strcmp(in, conv_table[i].in) &&
	   !strcmp(out, conv_table[i].out)){
	    return conv_table[i].conv;
	}
    }

    return NULL;
}

extern char **
audio_all_conv(char *in)
{
    char **cv = calloc(sizeof(*cv),
		       sizeof(conv_table) / sizeof(conv_table[0]));
    int i, j;

    cv[0] = in;
    for(i = 0, j = 1; conv_table[i].in; i++){
	if(!strcmp(in, conv_table[i].in) &&
	   strcmp(conv_table[i].in, conv_table[i].out))
	    cv[j++] = conv_table[i].out;
    }

    return cv;
}
