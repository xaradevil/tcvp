/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tctypes.h>
#include <unistd.h>
#include <tcbyteswap.h>
#include <audio_tc2.h>
#include <audiomod.h>

#define snd_conv(sname, stype, dname, dtype, conv)			\
static void								\
sname##_##dname(void *dst, void *src, int samples, int channels)	\
{									\
    stype *s = src;							\
    dtype *d = dst;							\
    int i;								\
									\
    for(i = 0; i < samples * channels; i++)				\
	d[i] = conv(s[i]);						\
}

#define s2u8(x) (x + 128)
#define s32s16(x) (x >> 16)

snd_conv(le16, int16_t, be16, int16_t, bswap_16)
snd_conv(s8, char, u8, u_char, s2u8)
snd_conv(s32, int32_t, s16, int16_t, s32s16);

#define copy(ss)						\
static void							\
copy_##ss(void *dst, void *src, int samples, int channels)	\
{								\
    memcpy(dst, src, samples * channels * ss / 8);		\
}

copy(8)
copy(16)

#define HE TCVP_ENDIAN

static struct {
    char *in;
    char *out;
    sndconv_t conv;
} conv_table[] = {
    { "s16le", "s16be", le16_be16 },
    { "s16be", "s16le", le16_be16 },
    { "u16le", "u16be", le16_be16 },
    { "u16be", "u16le", le16_be16 },
    { "s16le", "s16le", copy_16 },
    { "s16be", "s16be", copy_16 },
    { "u16le", "u16le", copy_16 },
    { "u16be", "u16be", copy_16 },
    { "u8",    "u8",    copy_8 },
    { "s8",    "s8",    copy_8 },
    { "s8",    "u8",    s8_u8 },
    { "s32"HE, "s16"HE, s32_s16 },
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
