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
#include <stdarg.h>
#include <tchash.h>
#include <tctypes.h>
#include <tcendian.h>
#include <tcvp_core_tc2.h>

/* key */

static void
key_free(void *p)
{
    tcvp_key_event_t *te = p;
    free(te->key);
}

extern void *
key_alloc(int type, va_list args)
{
    tcvp_key_event_t *te = tcvp_event_alloc(type, sizeof(*te), key_free);
    te->key = strdup(va_arg(args, char *));
    return te;
}

/* open */

static void
open_free(void *p)
{
    tcvp_open_event_t *te = p;
    free(te->file);
}

extern void *
open_alloc(int type, va_list args)
{
    tcvp_open_event_t *te = tcvp_event_alloc(type, sizeof(*te), open_free);
    te->file = strdup(va_arg(args, char *));
    return te;
}

extern u_char *
open_ser(char *name, void *event, int *size)
{
    tcvp_open_event_t *te = event;
    u_char *sb;
    int s;

    s = strlen(name) + strlen(te->file) + 2;
    sb = malloc(s);
    sprintf(sb, "%s%c%s", name, 0, te->file);
    *size = s;

    return sb;
}

extern void *
open_deser(int type, u_char *event, int size)
{
    u_char *n = memchr(event, 0, size);

    n++;

    if(!memchr(n, 0, size - (n - event)))
	return NULL;

    return tcvp_event_new(type, n);
}

/* open_multi */

static void
open_multi_free(void *p)
{
    tcvp_open_multi_event_t *te = p;
    int i;

    for(i = 0; i < te->nfiles; i++)
	free(te->files[i]);
    free(te->files);
}

extern void *
open_multi_alloc(int type, va_list args)
{
    tcvp_open_multi_event_t *te =
	tcvp_event_alloc(type, sizeof(*te), open_multi_free);
    char **files;
    int i;

    te->nfiles = va_arg(args, int);
    files = va_arg(args, char **);
    te->files = malloc(te->nfiles * sizeof(*te->files));
    for(i = 0; i < te->nfiles; i++)
	te->files[i] = strdup(files[i]);
    return te;
}

/* seek */

extern void *
seek_alloc(int type, va_list args)
{
    tcvp_seek_event_t *te = tcvp_event_alloc(type, sizeof(*te), NULL);
    te->time = va_arg(args, int64_t);
    te->how = va_arg(args, int);
    return te;
}

extern u_char *
seek_ser(char *name, void *event, int *size)
{
    tcvp_seek_event_t *te = event;
    int s = strlen(name) + 1 + 9;
    u_char *sb = malloc(s);
    u_char *p = sb;

    p += sprintf(sb, "%s", name);
    p++;
    st_unaligned64(htob_64(te->time), p);
    p += 8;
    *p = te->how;

    *size = s;
    return sb;
}

extern void *
seek_deser(int type, u_char *event, int size)
{
    u_char *n = memchr(event, 0, size);
    uint64_t time;
    int how;

    n++;

    if(size - (n - event) < 9)
	return NULL;

    time = htob_64(unaligned64(n));
    n += 8;
    how = *n;

    return tcvp_event_new(type, time, how);
}

/* timer */

extern void *
timer_alloc(int type, va_list args)
{
    tcvp_timer_event_t *te = tcvp_event_alloc(type, sizeof(*te), NULL);
    te->time = va_arg(args, uint64_t);
    return te;
}

extern u_char *
timer_ser(char *name, void *event, int *size)
{
    tcvp_timer_event_t *te = event;
    int s = strlen(name) + 1 + 8;
    u_char *sb = malloc(s);
    u_char *p = sb;

    p += sprintf(sb, "%s", name);
    p++;
    st_unaligned64(htob_64(te->time), p);

    *size = s;
    return sb;
}

extern void *
timer_deser(int type, u_char *event, int size)
{
    u_char *n = memchr(event, 0, size);
    uint64_t time;

    n++;
    if(size - (n - event) < 8)
	return NULL;

    time = htob_64(unaligned64(n));
    n += 8;

    return tcvp_event_new(type, time);
}

/* state */

extern void *
state_alloc(int type, va_list args)
{
    tcvp_state_event_t *te = tcvp_event_alloc(type, sizeof(*te), NULL);
    te->state = va_arg(args, int);
    return te;
}

extern u_char *
state_ser(char *name, void *event, int *size)
{
    tcvp_state_event_t *te = event;
    int s = strlen(name) + 1 + 4;
    u_char *sb = malloc(s);
    u_char *p = sb;

    p += sprintf(sb, "%s", name);
    p++;
    st_unaligned32(htob_32(te->state), p);

    *size = s;
    return sb;
}

extern void *
state_deser(int type, u_char *event, int size)
{
    u_char *n = memchr(event, 0, size);
    int state;

    n++;
    state = htob_32(unaligned32(n));
    return tcvp_event_new(type, state);
}

/* load */

static void
load_free(void *p)
{
    tcvp_load_event_t *te = p;
    tcfree(te->stream);
}

extern void *
load_alloc(int type, va_list args)
{
    tcvp_load_event_t *te = tcvp_event_alloc(type, sizeof(*te), load_free);
    te->stream = va_arg(args, muxed_stream_t *);
    tcref(te->stream);
    return te;
}

/* FIXME: incomplete serialization */
extern u_char *
load_ser(char *name, void *event, int *ssize)
{
    tcvp_load_event_t *te = event;
    char *file, *title, *artist, *performer, *album;
    u_char *sb, *p;
    int size, i;

#define get_attr(attr) do {				\
	if((attr = tcattr_get(te->stream, #attr)))	\
	    size += strlen(#attr) + strlen(attr) + 2;	\
    } while(0)

#define write_attr(attr) do {					\
	if(attr)						\
	    p += sprintf(p, "%s%c%s%c", #attr, 0, attr, 0);	\
    } while(0)

    size = strlen(name) + 1 + 8 + 1;
    get_attr(file);
    get_attr(title);
    get_attr(artist);
    get_attr(performer);
    get_attr(album);

    /* remember to update these sizes if serilization is modified below */
    for(i = 0; i < te->stream->n_streams; i++){
	stream_t *st = te->stream->streams + i;
	size += strlen(st->common.codec) + 1 + 1 + 1 + 4;
	if(st->stream_type == STREAM_TYPE_AUDIO){
	    size += 12;
	} else if(st->stream_type == STREAM_TYPE_VIDEO){
	    size += 16;
	}
    }

    sb = malloc(size);
    p = sb;
    p += sprintf(p, "%s", name);
    p++;
    st_unaligned64(htob_64(te->stream->time), p);
    p += 8;
    *p++ = te->stream->n_streams;

    /* remember to update the sizes above if changed */
    for(i = 0; i < te->stream->n_streams; i++){
	stream_t *st = te->stream->streams + i;
	*p++ = te->stream->used_streams[i];
	*p++ = st->stream_type;
	p += sprintf(p, "%s", st->common.codec);
	p++;
	st_unaligned32(htob_32(st->common.bit_rate), p);
	p += 4;
	if(st->stream_type == STREAM_TYPE_AUDIO){
	    st_unaligned32(htob_32(st->audio.sample_rate), p);
	    p += 4;
	    st_unaligned32(htob_32(st->audio.channels), p);
	    p += 4;
	    st_unaligned32(htob_32(st->audio.samples), p);
	    p += 4;
	} else if(st->stream_type == STREAM_TYPE_VIDEO){
	    st_unaligned32(htob_32(st->video.frame_rate.num), p);
	    p += 4;
	    st_unaligned32(htob_32(st->video.frame_rate.den), p);
	    p += 4;
	    st_unaligned32(htob_32(st->video.width), p);
	    p += 4;
	    st_unaligned32(htob_32(st->video.height), p);
	    p += 4;
	}
    }

    write_attr(file);
    write_attr(title);
    write_attr(artist);
    write_attr(performer);
    write_attr(album);
    *p = 0;

    *ssize = size;
    return sb;
}

static void
load_free_st(void *p)
{
    muxed_stream_t *ms = p;
    int i;

    for(i = 0; i < ms->n_streams; i++)
	free(ms->streams[i].common.codec);
    free(ms->streams);
    free(ms->used_streams);
}

extern void *
load_deser(int type, u_char *event, int size)
{
    u_char *n = memchr(event, 0, size);
    muxed_stream_t *ms;
    tcvp_event_t *te = NULL;
    int i;

#define get32(d) do {					\
	d = htob_32(unaligned32(n));			\
	n += 4;						\
	size -= 4;					\
	tc2_print("TCVP", TC2_PRINT_DEBUG,		\
		  "load_deser: "#d" = %i\n", d);	\
    } while(0);

    if(!n)
	return NULL;
    n++;
    size -= n - event;
    if(size < 9)
	return NULL;

    ms = tcallocdz(sizeof(*ms), NULL, load_free_st);
    ms->time = htob_64(unaligned64(n));
    n += 8;
    size -= 8;

    ms->n_streams = *n++;
    size--;
    tc2_print("TCVP", TC2_PRINT_DEBUG,
	      "load_deser: %i streams\n", ms->n_streams);
    ms->streams = calloc(ms->n_streams, sizeof(*ms->streams));
    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    for(i = 0; i < ms->n_streams && size > 0; i++){
	u_char *ce;
	stream_t *st = ms->streams + i;

	ms->used_streams[i] = *n++;
	size--;
	st->stream_type = *n++;
	size--;
	ce = memchr(n, 0, size);
	if(!ce){
	    tc2_print("TCVP", TC2_PRINT_WARNING, "load_deser: short buffer\n");
	    goto out;
	}
	tc2_print("TCVP", TC2_PRINT_DEBUG, "load_deser: codec = %s\n", n);
	st->common.codec = strdup(n);
	ce++;
	size -= ce - n;
	n = ce;
	get32(st->common.bit_rate);
	if(st->stream_type == STREAM_TYPE_AUDIO){
	    get32(st->audio.sample_rate);
	    get32(st->audio.channels);
	    get32(st->audio.samples);
	} else if(st->stream_type == STREAM_TYPE_VIDEO){
	    get32(st->video.frame_rate.num);
	    get32(st->video.frame_rate.den);
	    get32(st->video.width);
	    get32(st->video.height);
	}
    }

    while(size > 0 && *n){
	u_char *v = memchr(n, 0, size);
	u_char *na;

	if(!v)
	    break;
	v++;
	size -= v - n;
	na = memchr(v, 0, size);
	if(!na)
	    break;
	na++;
	size -= na - v;
	tcattr_set(ms, n, strdup(v), NULL, free);
	tc2_print("TCVP", TC2_PRINT_DEBUG, "load_deser: %s = %s\n", n, v);
	n = na;
    }

    te = tcvp_event_new(type, ms);
out:
    tcfree(ms);
    return te;
#undef get32
}

/* button */

extern void *
button_alloc(int type, va_list args)
{
    tcvp_button_event_t *te = tcvp_event_alloc(type, sizeof(*te), NULL);
    te->button = va_arg(args, int);
    te->action = va_arg(args, int);
    te->x = va_arg(args, int);
    te->y = va_arg(args, int);

    return te;
}
