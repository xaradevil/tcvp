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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <tchash.h>
#include <tcvp_mod.h>
#include <tcvp_core_tc2.h>

typedef struct tcvp_event_type {
    char *name;
    int num;
    tcvp_event_alloc_t alloc;
} tcvp_event_type_t;

static hash_table *event_types;
static tcvp_event_type_t **event_tab;

static int event_num;

static void*
evt_alloc(int type, va_list args)
{
    tcvp_event_t *te = alloc_event(type, sizeof(*te), NULL);
    return te;
}

extern int
reg_event(char *name, tcvp_event_alloc_t af)
{
    tcvp_event_type_t *e;

    if(!hash_find(event_types, name, &e))
	return -1;

    if(!af)
	af = evt_alloc;

    e = malloc(sizeof(*e));
    e->name = strdup(name);
    e->num = ++event_num;
    e->alloc = af;

    event_tab = realloc(event_tab, (event_num + 1) * sizeof(*event_tab));
    event_tab[e->num] = e;

    hash_replace(event_types, name, e);

    return e->num;
}

extern int
get_event(char *name)
{
    tcvp_event_type_t *e;

    if(hash_find(event_types, name, &e))
	return -1;

    return e->num;
}

static void
free_event(void *p)
{
    tcvp_event_type_t *e = p;
    event_tab[e->num] = NULL;
    free(e->name);
    free(e);
}

extern int
del_event(char *name)
{
    tcvp_event_type_t *e;

    if(!hash_delete(event_types, name, &e))
	free_event(e);

    return 0;
}

extern int
send_event(eventq_t q, int type, ...)
{
    tcvp_event_t *te = NULL;
    va_list args;
    int ret = -1;

    va_start(args, type);

    if(type == -1){
	te = tcalloc(sizeof(*te));
	te->type = -1;
    } else if(event_tab[type]){
	te = event_tab[type]->alloc(type, args);
    }

    va_end(args);

    if(te){
	ret = eventq_send(q, te);
	tcfree(te);
    }

    return ret;
}

extern void *
alloc_event(int type, int size, tc_free_fn ff)
{
    tcvp_event_t *te;

    te = tcallocdz(size, NULL, ff);
    te->type = type;

    return te;
}

static void
key_free(void *p)
{
    tcvp_key_event_t *te = p;
    free(te->key);
}

static void *
key_alloc(int type, va_list args)
{
    tcvp_key_event_t *te = alloc_event(type, sizeof(*te), key_free);
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
    tcvp_open_event_t *te = alloc_event(type, sizeof(*te), open_free);
    te->file = strdup(va_arg(args, char *));
    return te;
}

static void *
seek_alloc(int type, va_list args)
{
    tcvp_seek_event_t *te = alloc_event(type, sizeof(*te), NULL);
    te->time = va_arg(args, int64_t);
    te->how = va_arg(args, int);
    return te;
}

static void *
timer_alloc(int type, va_list args)
{
    tcvp_timer_event_t *te = alloc_event(type, sizeof(*te), NULL);
    te->time = va_arg(args, uint64_t);
    return te;
}

static void *
state_alloc(int type, va_list args)
{
    tcvp_state_event_t *te = alloc_event(type, sizeof(*te), NULL);
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
    tcvp_load_event_t *te = alloc_event(type, sizeof(*te), load_free);
    te->stream = va_arg(args, muxed_stream_t *);
    tcref(te->stream);
    return te;
}

static tcvp_event_type_t core_events[] = {
    { .name = "TCVP_KEY",         .alloc = key_alloc   },
    { .name = "TCVP_OPEN",        .alloc = open_alloc  },
    { .name = "TCVP_START",       .alloc = evt_alloc   },
    { .name = "TCVP_STOP",        .alloc = evt_alloc   },
    { .name = "TCVP_PAUSE",       .alloc = evt_alloc   },
    { .name = "TCVP_SEEK",        .alloc = seek_alloc  },
    { .name = "TCVP_CLOSE",       .alloc = evt_alloc   },
    { .name = "TCVP_TIMER",       .alloc = timer_alloc },
    { .name = "TCVP_STATE",       .alloc = state_alloc },
    { .name = "TCVP_LOAD",        .alloc = load_alloc  },
    { .name = "TCVP_STREAM_INFO", .alloc = evt_alloc   },
};

extern int
init_events(void)
{
    int i;

    event_types = hash_new(20, 0);

    for(i = 0; i < sizeof(core_events) / sizeof(core_events[0]); i++)
	reg_event(core_events[i].name, core_events[i].alloc);

    return 0;
}

extern void
free_events(void)
{
    hash_destroy(event_types, free_event);
    if(event_tab)
	free(event_tab);
    event_num = 0;
}
