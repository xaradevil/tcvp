/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <tchash.h>
#include <tctypes.h>
#include <tcbyteswap.h>
#include <tcvp_mod.h>
#include <tcvp_core_tc2.h>

static void
key_free(void *p)
{
    tcvp_key_event_t *te = p;
    free(te->key);
}

static void *
key_alloc(int type, va_list args)
{
    tcvp_key_event_t *te = tcvp_event_alloc(type, sizeof(*te), key_free);
    te->key = strdup(va_arg(args, char *));
    return te;
}

static void
open_free(void *p)
{
    tcvp_open_event_t *te = p;
    free(te->file);
}

static void
open_multi_free(void *p)
{
    tcvp_open_multi_event_t *te = p;
    int i;

    for(i = 0; i < te->nfiles; i++)
	free(te->files[i]);
    free(te->files);
}

static void *
open_alloc(int type, va_list args)
{
    tcvp_open_event_t *te = tcvp_event_alloc(type, sizeof(*te), open_free);
    te->file = strdup(va_arg(args, char *));
    return te;
}

static u_char *
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

static void *
open_deser(int type, u_char *event, int size)
{
    u_char *n = memchr(event, 0, size);

    n++;

    if(!memchr(n, 0, size - (n - event)))
	return NULL;

    return tcvp_event_new(type, n);
}

static void *
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

static void *
seek_alloc(int type, va_list args)
{
    tcvp_seek_event_t *te = tcvp_event_alloc(type, sizeof(*te), NULL);
    te->time = va_arg(args, int64_t);
    te->how = va_arg(args, int);
    return te;
}

static u_char *
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

static void *
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

static void *
timer_alloc(int type, va_list args)
{
    tcvp_timer_event_t *te = tcvp_event_alloc(type, sizeof(*te), NULL);
    te->time = va_arg(args, uint64_t);
    return te;
}

static u_char *
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

static void *
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

static void *
state_alloc(int type, va_list args)
{
    tcvp_state_event_t *te = tcvp_event_alloc(type, sizeof(*te), NULL);
    te->state = va_arg(args, int);
    return te;
}

static u_char *
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

static void *
state_deser(int type, u_char *event, int size)
{
    u_char *n = memchr(event, 0, size);
    int state;

    n++;
    state = htob_32(unaligned32(n));
    return tcvp_event_new(type, state);
}

static void
load_free(void *p)
{
    tcvp_load_event_t *te = p;
    tcfree(te->stream);
}

static void *
load_alloc(int type, va_list args)
{
    tcvp_load_event_t *te = tcvp_event_alloc(type, sizeof(*te), load_free);
    te->stream = va_arg(args, muxed_stream_t *);
    tcref(te->stream);
    return te;
}

static struct {
    char *name;
    tcvp_alloc_event_t alloc;
    tcvp_serialize_event_t serialize;
    tcvp_deserialize_event_t deserialize;
} core_events[] = {
    { "TCVP_KEY",         key_alloc,        NULL,      NULL        },
    { "TCVP_OPEN",        open_alloc,       open_ser,  open_deser  },
    { "TCVP_OPEN_MULTI",  open_multi_alloc, NULL,      NULL        },
    { "TCVP_START",       NULL,             NULL,      NULL        },
    { "TCVP_STOP",        NULL,             NULL,      NULL        },
    { "TCVP_PAUSE",       NULL,             NULL,      NULL        },
    { "TCVP_SEEK",        seek_alloc,       seek_ser,  seek_deser  },
    { "TCVP_CLOSE",       NULL,             NULL,      NULL        },
    { "TCVP_TIMER",       timer_alloc,      timer_ser, timer_deser },
    { "TCVP_STATE",       state_alloc,      state_ser, state_deser },
    { "TCVP_LOAD",        load_alloc,       NULL,      NULL        },
    { "TCVP_STREAM_INFO", NULL,             NULL,      NULL        },
};

extern int
init_events(void)
{
    int i;

    for(i = 0; i < sizeof(core_events) / sizeof(core_events[0]); i++)
	tcvp_event_register(core_events[i].name, core_events[i].alloc,
			    core_events[i].serialize,
			    core_events[i].deserialize);

    return 0;
}
