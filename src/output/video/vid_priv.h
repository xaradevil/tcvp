/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#ifndef _VID_PRIV_H
#define _VID_PRIV_H

#include <stdint.h>

typedef void (*color_conv_t)(int, int, const u_char **, int *, u_char **,
			     int *);
extern color_conv_t get_cconv(char *, char *);

#endif
