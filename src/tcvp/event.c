/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <tchash.h>
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

static void *
open_alloc(int type, va_list args)
{
    tcvp_open_event_t *te = tcvp_event_alloc(type, sizeof(*te), open_free);
    te->file = strdup(va_arg(args, char *));
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

static void *
timer_alloc(int type, va_list args)
{
    tcvp_timer_event_t *te = tcvp_event_alloc(type, sizeof(*te), NULL);
    te->time = va_arg(args, uint64_t);
    return te;
}

static void *
state_alloc(int type, va_list args)
{
    tcvp_state_event_t *te = tcvp_event_alloc(type, sizeof(*te), NULL);
    te->state = va_arg(args, int);
    return te;
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
} core_events[] = {
    { .name = "TCVP_KEY",         .alloc = key_alloc   },
    { .name = "TCVP_OPEN",        .alloc = open_alloc  },
    { .name = "TCVP_START",       .alloc = NULL        },
    { .name = "TCVP_STOP",        .alloc = NULL        },
    { .name = "TCVP_PAUSE",       .alloc = NULL        },
    { .name = "TCVP_SEEK",        .alloc = seek_alloc  },
    { .name = "TCVP_CLOSE",       .alloc = NULL        },
    { .name = "TCVP_TIMER",       .alloc = timer_alloc },
    { .name = "TCVP_STATE",       .alloc = state_alloc },
    { .name = "TCVP_LOAD",        .alloc = load_alloc  },
    { .name = "TCVP_STREAM_INFO", .alloc = NULL        },
};

extern int
init_events(void)
{
    int i;

    for(i = 0; i < sizeof(core_events) / sizeof(core_events[0]); i++)
	tcvp_event_register(core_events[i].name, core_events[i].alloc);

    return 0;
}
