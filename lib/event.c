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
#include <tcvp_event.h>

static void
evt_free(void *p)
{
    tcvp_event_t *te = p;

    switch(te->type){
    case TCVP_LOAD:
	tcfree(te->load.stream);
	break;
    }
}


extern void *
tcvp_alloc_event(int type, ...)
{
    tcvp_event_t *te;
    va_list args;

    va_start(args, type);

    te = tcallocd(sizeof(*te), NULL, evt_free);
    memset(te, 0, sizeof(*te));
    te->type = type;

    switch(type){
    case TCVP_KEY:
	te->key.key = va_arg(args, char *);
	break;

    case TCVP_OPEN:
	te->open.file = va_arg(args, char *);
	break;

    case TCVP_SEEK:
	te->seek.time = va_arg(args, int64_t);
	te->seek.how = va_arg(args, int);
	break;

    case TCVP_TIMER:
	te->timer.time = va_arg(args, uint64_t);
	break;

    case TCVP_STATE:
	te->state.state = va_arg(args, int);
	break;

    case TCVP_LOAD:
	te->load.stream = va_arg(args, muxed_stream_t *);
	tcref(te->load.stream);
	break;

    case TCVP_START:
    case TCVP_STOP:
    case TCVP_PAUSE:
    case TCVP_CLOSE:
    case TCVP_STREAM_INFO:
    case TCVP_PL_START:
    case TCVP_PL_STOP:
    case TCVP_PL_NEXT:
    case TCVP_PL_PREV:
    case -1:
	break;

    default:
	fprintf(stderr, "%s: unknown event type %i\n", __FUNCTION__, type);
    }

    return te;
}
