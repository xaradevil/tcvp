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
#include <tcstring.h>
#include <tctypes.h>
#include <tcvp_types.h>
#include <codec_tc2.h>

#define suffix_map tcvp_codec_conf_suffix
#define suffix_map_size tcvp_codec_conf_suffix_count

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

extern tcvp_pipe_t *
enc_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    encoder_new_t cnew = NULL;
    char *name, *sf;
    char *m = NULL;

    if(tcconf_getvalue(cs, "mux/url", "%s", &name) <= 0)
	return NULL;

    if((sf = strrchr(name, '.'))){
	int i;
	for(i = 0; i < suffix_map_size; i++){
	    if(!strcmp(sf, suffix_map[i].suffix)){
		if(s->stream_type == STREAM_TYPE_VIDEO)
		    m = suffix_map[i].vcodec;
		else if(s->stream_type == STREAM_TYPE_AUDIO)
		    m = suffix_map[i].acodec;
		else
		    fprintf(stderr, "ENCODE: unknown stream type %i\n",
			    s->stream_type);
		break;
	    }
	}
    }

    if(m){
	char mb[strlen(m) + 8];
	sprintf(mb, "encoder/%s", m);
	cnew = tc2_get_symbol(mb, "new");
    }

    free(name);
    return cnew? cnew(s, cs, t, ms): NULL;
}
