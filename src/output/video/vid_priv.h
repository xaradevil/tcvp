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

#ifndef _VID_PRIV_H
#define _VID_PRIV_H

#include <stdint.h>

typedef void (*color_conv_t)(int, const u_char **, int *, u_char **, int *);

extern void i420_yuy2(int height, const u_char **in, int *istride,
		      u_char **out, int *ostride);
extern void i420_yv12(int height, const u_char **in, int *istride,
		      u_char **out, int *ostride);
extern void yv12_i420(int height, const u_char **in, int *istride,
		      u_char **out, int *ostride);

#endif
