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

#ifndef _TCVP_EVENT_H
#define _TCVP_EVENT_H

#include <tcvp_types.h>
#include <tcalloc.h>

typedef struct tcvp_key_event {
    int type;
    char *key;
} tcvp_key_event_t;

typedef struct tcvp_open_event {
    int type;
    char *file;
} tcvp_open_event_t;

typedef struct tcvp_seek_event {
    int type;
    int64_t time;
    int how;
} tcvp_seek_event_t;

typedef struct tcvp_timer_event {
    int type;
    uint64_t time;
} tcvp_timer_event_t;

typedef struct tcvp_state_event {
    int type;
    int state;
} tcvp_state_event_t;

typedef struct tcvp_load_event {
    int type;
    muxed_stream_t *stream;
} tcvp_load_event_t;

typedef struct tcvp_pl_add_event {
    int type;
    char **names;
    int n;
    int pos;
} tcvp_pl_add_event_t;

typedef struct tcvp_pl_addlist_event {
    int type;
    char *name;
    int pos;
} tcvp_pl_addlist_event_t;

typedef struct tcvp_pl_remove_event {
    int type;
    int start;
    int n;
} tcvp_pl_remove_event_t;

typedef struct tcvp_pl_shuffle_event {
    int type;
    int start;
    int n;
} tcvp_pl_shuffle_event_t;

typedef union tcvp_event {
    int type;
    tcvp_key_event_t key;
    tcvp_open_event_t open;
    tcvp_seek_event_t seek;
    tcvp_timer_event_t timer;
    tcvp_state_event_t state;
    tcvp_load_event_t load;
    tcvp_pl_add_event_t pl_add;
    tcvp_pl_addlist_event_t pl_addlist;
    tcvp_pl_remove_event_t pl_remove;
    tcvp_pl_shuffle_event_t pl_shuffle;
} tcvp_event_t;

#define TCVP_KEY       1
#define TCVP_OPEN      2
#define TCVP_START     3
#define TCVP_STOP      4
#define TCVP_PAUSE     5

#define TCVP_SEEK      6
#define TCVP_SEEK_ABS  0
#define TCVP_SEEK_REL  1

#define TCVP_CLOSE     7
#define TCVP_TIMER     8

#define TCVP_STATE     9
#define TCVP_STATE_PLAYING 0
#define TCVP_STATE_END     1
#define TCVP_STATE_ERROR   2
#define TCVP_STATE_STOPPED 3
#define TCVP_STATE_PL_END  4

#define TCVP_LOAD     10
#define TCVP_STREAM_INFO 11

#define TCVP_PL_START   12
#define TCVP_PL_STOP    13
#define TCVP_PL_NEXT    14
#define TCVP_PL_PREV    15
#define TCVP_PL_ADD     16
#define TCVP_PL_ADDLIST 17
#define TCVP_PL_REMOVE  18
#define TCVP_PL_SHUFFLE 19

extern void *tcvp_alloc_event(int type, ...);

#endif
