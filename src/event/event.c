/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <tchash.h>
#include <tcalloc.h>
#include <tcvp_event_tc2.h>

typedef struct tcvp_event_type {
    char *name;
    int num;
    tcvp_alloc_event_t alloc;
} tcvp_event_type_t;

static hash_table *event_types;
static tcvp_event_type_t **event_tab;

static int event_num;

static void *
evt_alloc(int type, va_list args)
{
    tcvp_event_t *te = alloc_event(type, sizeof(*te), NULL);
    return te;
}

static tcvp_event_type_t *
new_type(char *name, tcvp_alloc_event_t af)
{
    tcvp_event_type_t *e;

    e = malloc(sizeof(*e));
    e->name = strdup(name);
    e->num = ++event_num;
    e->alloc = af;

    event_tab = realloc(event_tab, (event_num + 1) * sizeof(*event_tab));
    event_tab[e->num] = e;
    hash_replace(event_types, name, e);

    return e;
}

extern int
reg_event(char *name, tcvp_alloc_event_t af)
{
    tcvp_event_type_t *e;

    if(!af)
	af = evt_alloc;

    if(!hash_find(event_types, name, &e)){
	if(e->alloc)
	    return -1;
	e->alloc = af;
	return e->num;
    }

    e = new_type(name, af);

    return e->num;
}

extern int
get_event(char *name)
{
    tcvp_event_type_t *e;

    if(hash_find(event_types, name, &e))
	e = new_type(name, NULL);

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
    } else if(type <= event_num && event_tab[type]){
	te = event_tab[type]->alloc(type, args);
    } else {
	fprintf(stderr, "TCVP: unknown event type %i\n", type);
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

extern int
event_init(char *p)
{
    event_types = hash_new(20, 0);
    return 0;
}

extern int
event_free(void)
{
    hash_destroy(event_types, free_event);
    if(event_tab)
	free(event_tab);
    event_num = 0;

    return 0;
}
