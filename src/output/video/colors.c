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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <tcvp_types.h>
#include <video_tc2.h>
#include "vid_priv.h"

#ifndef min
#define min(a,b) ((a)<(b)? (a): (b))
#endif

static void
i420_yuy2(int width, int height, const u_char **in, int *istride,
	  u_char **out, int *ostride)
{
    int y;
    const u_char *ysrc = in[0];
    const u_char *usrc = in[1];
    const u_char *vsrc = in[2];
    const u_char *dst = out[0];

    for(y = 0; y < height; y++){
#if __WORDSIZE >= 64
	int i;
	uint64_t *ldst = (uint64_t *) dst;
	const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
	for(i = 0; i < min(istride[0], ostride[0]); i += 4){
	    *ldst++ = (uint64_t)yc[0] + ((uint64_t)uc[0] << 8) +
		((uint64_t)yc[1] << 16) + ((uint64_t)vc[0] << 24) +
		((uint64_t)yc[2] << 32) + ((uint64_t)uc[1] << 40) +
		((uint64_t)yc[3] << 48) + ((uint64_t)vc[1] << 56);
	    yc += 4;
	    uc += 2;
	    vc += 2;
	}
#else
	int i, *idst = (int32_t *) dst;
	const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
	for(i = 0; i < istride[1]; i++){
	    *idst++ = yc[0] + (uc[0] << 8) +
		(yc[1] << 16) + (vc[0] << 24);
	    yc += 2;
	    uc++;
	    vc++;
	}
#endif
	ysrc += istride[0];
	if(y & 1){
	    usrc += istride[1];
	    vsrc += istride[2];
	}
	dst += ostride[0];
    }
}

#define copy_plane(i, ip, d) do {				\
    int j;							\
    for(j = 0; j < height / d; j++){				\
	memcpy(out[i] + j * ostride[i],				\
	       in[ip] + j * istride[i],				\
	       min(istride[i], min(ostride[i], width / d)));	\
    }								\
} while(0)

#define exp_plane(i, ip, d, x, y) do {					    \
    int j, k;								    \
    int w = min(istride[i], min(ostride[i], width / d));		    \
    for(j = 0; j < height / d; j++){					    \
	for(k = 0; k < w; k++){						    \
	    int p = *(in[ip] + j * istride[i] + k);			    \
	    int xx, yy;							    \
	    for(xx = 0; xx < x; xx++)					    \
		for(yy = 0; yy < y; yy++)				    \
		    *(out[i] + (y * j + yy) * ostride[i] + x * k + xx) = p; \
	}								    \
    }									    \
} while(0)

#define red_plane(i, ip, dx, dy, x, y) do {			\
    int j, k;							\
    int w = min(istride[i], min(ostride[i], width / dx));	\
    for(j = 0; j < height / dy / y; j++){			\
	for(k = 0; k < w / x; k++){				\
	    *(out[i] + j * ostride[i] + k) =			\
		*(in[ip] + j * y * istride[i] + k * x);		\
	}							\
    }								\
} while(0)
		

#define copy_planar(fin, fout, p0, p1, p2, d0, d1, d2)			\
static void								\
fin##_##fout(int width, int height, const u_char **in, int *istride,	\
	     u_char **out, int *ostride)				\
{									\
    copy_plane(0, p0, d0);						\
    copy_plane(1, p1, d1);						\
    copy_plane(2, p2, d2);						\
}

#define alias(f1, f2) f1() __attribute__((alias(#f2)))

copy_planar(yv12, yv12, 0, 1, 2, 1, 2, 2)
copy_planar(i420, yv12, 0, 2, 1, 1, 2, 2)
alias(void yv12_i420, i420_yv12);
alias(void i420_i420, yv12_yv12);

static void
yvu9_yv12(int width, int height, const u_char **in, int *istride,
	  u_char **out, int *ostride)
{
    copy_plane(0, 0, 1);
    exp_plane(1, 2, 4, 2, 2);
    exp_plane(2, 1, 4, 2, 2);
}

static void
yuv422p_yv12(int width, int height, const u_char **in, int *istride,
	     u_char **out, int *ostride)
{
    copy_plane(0, 0, 1);
    red_plane(1, 2, 2, 1, 1, 2);
    red_plane(2, 1, 2, 1, 1, 2);
}

#define cctab(in, out) { #in, #out, in##_##out }

static struct {
    char *in;
    char *out;
    color_conv_t conv;
} conv_table[] = {
    cctab(i420, i420),
    cctab(i420, i420),
    cctab(yv12, yv12),
    cctab(i420, yv12),
    cctab(yv12, i420),
    cctab(i420, yuy2),
    cctab(yvu9, yv12),
    cctab(yuv422p, yv12),
    { NULL, NULL, NULL }
};

extern color_conv_t
get_cconv(char *in, char *out)
{
    int i;

    for(i = 0; conv_table[i].in; i++)
	if(!strcmp(in, conv_table[i].in) && !strcmp(out, conv_table[i].out))
	    break;

    return conv_table[i].conv;
}
