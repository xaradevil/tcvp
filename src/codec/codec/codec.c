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
c_new(stream_t *s, int mode)
{
    char *codec;
    codec_new_t cnew;
    char *buf;

    switch(s->stream_type){
    case STREAM_TYPE_AUDIO:
	codec = s->audio.codec;
	break;

    case STREAM_TYPE_VIDEO:
	codec = s->video.codec;
	break;

    default:
	return NULL;
    }

    buf = alloca(strlen(codec) + 8);
    sprintf(buf, "codec/%s", codec);
    if(!(cnew = tc2_get_symbol(buf, "new")))
	return NULL;

    return cnew(s, mode);
}
