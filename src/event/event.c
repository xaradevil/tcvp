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
#include <tcalloc.h>
#include <tcvp_event_tc2.h>

#undef DEBUG

typedef struct tcvp_event_type {
    char *name;
    int num;
    tcvp_alloc_event_t *alloc;
    tcvp_serialize_event_t *serialize;
    tcvp_deserialize_event_t *deserialize;
} tcvp_event_type_t;

static tchash_table_t *event_types;
static tcvp_event_type_t **event_tab;

static int event_num;

static void *
evt_alloc(int type, va_list args)
{
    tcvp_event_t *te = alloc_event(type, sizeof(*te), NULL);
    return te;
}

static u_char *
evt_serialize(char *name, void *event, int *size)
{
    *size = strlen(name) + 1;
    return strdup(name);
}

static void *
evt_deserialize(int type, u_char *event, int size)
{
    return new_event(type);
}

static tcvp_event_type_t *
new_type(char *name, tcvp_alloc_event_t af, tcvp_serialize_event_t sf,
	 tcvp_deserialize_event_t df)
{
    tcvp_event_type_t *e;

    e = malloc(sizeof(*e));
    e->name = strdup(name);
    e->num = ++event_num;
    e->alloc = af;
    e->serialize = sf;
    e->deserialize = df;

    event_tab = realloc(event_tab, (event_num + 1) * sizeof(*event_tab));
    event_tab[e->num] = e;
    tchash_replace(event_types, name, e);

#ifdef DEBUG
    fprintf(stderr, "EVENT: registered %s as %i\n", name, e->num);
#endif

    return e;
}

extern int
reg_event(char *name, tcvp_alloc_event_t af, tcvp_serialize_event_t sf,
	  tcvp_deserialize_event_t df)
{
    tcvp_event_type_t *e;

    if(!af){
	af = evt_alloc;
	if(!sf)
	    sf = evt_serialize;
	if(!df)
	    df = evt_deserialize;
    }

    if(!tchash_find(event_types, name, &e)){
	if(e->alloc)
	    return -1;
	e->alloc = af;
	e->serialize = sf;
	e->deserialize = df;
	return e->num;
    }

    e = new_type(name, af, sf, df);

    return e->num;
}

extern int
get_event(char *name)
{
    tcvp_event_type_t *e;

    if(tchash_find(event_types, name, &e))
	e = new_type(name, NULL, NULL, NULL);

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

    if(!tchash_delete(event_types, name, &e))
	free_event(e);

    return 0;
}

extern void *
new_event(int type, ...)
{
    tcvp_event_t *te = NULL;
    va_list args;

    va_start(args, type);

    if(type <= event_num && event_tab[type]){
	te = event_tab[type]->alloc(type, args);
    } else {
	fprintf(stderr, "TCVP: unknown event type %i\n", type);
    }

    va_end(args);

    return te;
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
    } else if(type <= event_num && event_tab[type] && event_tab[type]->alloc){
	te = event_tab[type]->alloc(type, args);
    } else {
	fprintf(stderr, "TCVP: unknown event type %i\n", type);
    }

    va_end(args);

    if(te){
#ifdef DEBUG
	if(type >= 0)
	    fprintf(stderr, "EVENT: sending %s\n", event_tab[type]->name);
#endif
	ret = eventq_send(q, te);
	tcfree(te);
    }

    return ret;
}

extern u_char *
serialize_event(void *event, int *size)
{
    tcvp_event_t *te = event;


    if(te->type < 0 || te->type > event_num)
	return NULL;
    if(!event_tab[te->type])
	return NULL;

    if(!event_tab[te->type]->serialize){
	fprintf(stderr, "EVENT: no serializer for %s\n",
		event_tab[te->type]->name);
	return NULL;
    }

    return event_tab[te->type]->serialize(event_tab[te->type]->name,
					  event, size);
}

extern void *
deserialize_event(u_char *event, int size)
{
    tcvp_event_type_t *e;

    if(!memchr(event, 0, size))
	return NULL;

    if(tchash_find(event_types, event, &e))
	return NULL;

    if(!e->deserialize)
	return NULL;

    return e->deserialize(e->num, event, size);
}

extern void *
alloc_event(int type, int size, tcfree_fn ff)
{
    tcvp_event_t *te;

    te = tcallocdz(size, NULL, ff);
    te->type = type;

    return te;
}

extern int
event_init(char *p)
{
    event_types = tchash_new(20, 0);
    return 0;
}

extern int
event_free(void)
{
    tchash_destroy(event_types, free_event);
    if(event_tab)
	free(event_tab);
    event_num = 0;

    return 0;
}
