/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcvp_types.h>
#include <codec_tc2.h>

extern tcvp_pipe_t *
dec_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
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

    return cnew(s, cs, t, ms);
}
