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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <tcvp_types.h>
#include <video_tc2.h>

#ifndef min
#define min(a,b) ((a)<(b)? (a): (b))
#endif

extern void
i420_yuy2(int height, const u_char **in, int *istride,
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
	for(i = 0; i < istride[1]; i += 2){
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

extern void
i420_yv12(int height, const u_char **in, int *istride,
	  u_char **out, int *ostride)
{
    const char *src[] = {in[0], in[2], in[1]};
    int i, j;

    for(i = 0; i < 3; i++){
	for(j = 0; j < height / (i? 2: 1); j++){
	    memcpy(out[i] + j * ostride[i],
		   src[i] + j * istride[i],
		   min(istride[i], ostride[i]));
	}
    }
}

extern void yv12_i420()__attribute__((alias("i420_yv12")));