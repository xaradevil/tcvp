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

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcvp_types.h>
#include <codec_tc2.h>

extern tcvp_pipe_t *
dec_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t)
{
    char *codec = NULL;
    decoder_new_t cnew;
    char *buf;

    codec = s->common.codec;

    if(!codec)
	return NULL;

    buf = alloca(strlen(codec) + 9);
    sprintf(buf, "decoder/%s", codec);
    if(!(cnew = tc2_get_symbol(buf, "new")))
	return NULL;

    return cnew(s, cs, t);
}
