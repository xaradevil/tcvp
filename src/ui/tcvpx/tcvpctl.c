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

#include "tcvpx.h"
#include <tcvp_event.h>
#include "tcvpctl.h"
#include <string.h>
#include <unistd.h>

static int tcvpstate=-1;

extern void *
tcvp_event(void *p)
{
    skin_t *skin = p;
    muxed_stream_t *st = NULL;
    char *title;

    while(!quit){
	tcvp_event_t *te = eventq_recv(qr);
/* 	printf("%d\n", te->type); */
	switch(te->type){

	case TCVP_STATE:
	    tcvpstate = te->state.state;
	    switch(te->state.state) {
	    case TCVP_STATE_PL_END:
		tcvp_stop(NULL, NULL);
		break;

	    case TCVP_STATE_PLAYING:
		update_state("play");
		break;

	    case TCVP_STATE_STOPPED:
		update_state("pause");
		break;

	    case TCVP_STATE_END:
		update_state("stop");
		break;
	    }
	    break;

	case TCVP_TIMER:
	    s_time = te->timer.time/1000000;
	    if(s_time > s_length)
		s_length = s_time;
	    update_time();
	    break;

	case TCVP_LOAD:
	    if(st)
		tcfree(st);
	    st = te->load.stream;
	    tcref(st);

/* 	    printf("%s %s %s\n", st->title, st->performer, st->file); */
	    if(st->title){
		if(st->performer){
		    title = malloc(strlen(st->title)+strlen(st->performer)+4);
		    sprintf(title, "%s - %s", st->performer, st->title);
		} else {
		    title = malloc(strlen(st->title)+1);
		    strcpy(title, st->title);
		}
	    } else {
		char *ext;
		title = strrchr(st->file, '/');
		title = strdup(title? title + 1: te->load.stream->file);
		ext = strrchr(title, '.');
		if(ext)
		    *ext = 0;
	    }

	    update_title(title);
	    free(title);

	    /* fall through */

	case TCVP_STREAM_INFO:
	    if(!st)
		break;

	    if(st->time)
		s_length = st->time / 1000000;

/* 	    printf("%ld (%d)\n", samples, sample_rate); */
/* 	    printf("%ld (%d/%d)\n", frames, frame_rate_num, frame_rate_den); */
/* 	    printf("%d\n", s_length); */

	    if(skin->seek_bar) {
		if(s_length > 0){
		    enable_seek_bar(skin->seek_bar);
		} else {
		    disable_seek_bar(skin->seek_bar);
		}
	    }
	    break;
	}
	tcfree(te);
    }
    return NULL;
}


extern int
tcvp_pause(tcwidget_t *w, void *p)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PAUSE);
    eventq_send(qs, te);
    tcfree(te);
    return 0;
}


extern int
tcvp_stop(tcwidget_t *w, void *p)
{
    tcvp_event_t *te;
    te = tcvp_alloc_event(TCVP_PL_STOP);
    eventq_send(qs, te);
    tcfree(te);

    te = tcvp_alloc_event(TCVP_CLOSE);
    eventq_send(qs, te);
    tcfree(te);

    return 0;
}


extern int
tcvp_play(tcwidget_t *w, void *p)
{
    if(tcvpstate == TCVP_STATE_STOPPED) {
	tcvp_pause(w, p);
    } else {
	if(tcvpstate != TCVP_STATE_PLAYING) {
	    tcvp_event_t *te = tcvp_alloc_event(TCVP_PL_START);
	    eventq_send(qs, te);
	    tcfree(te);
	} else {
	    tcvp_stop(w, p);

	    tcvp_event_t *te = tcvp_alloc_event(TCVP_PL_START);
	    eventq_send(qs, te);
	    tcfree(te);
	}
    }

    return 0;
}


extern int
tcvp_next(tcwidget_t *w, void *p)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PL_NEXT);
    eventq_send(qs, te);
    tcfree(te);

    return 0;
}


extern int
tcvp_previous(tcwidget_t *w, void *p)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PL_PREV);
    eventq_send(qs, te);
    tcfree(te);

    return 0;
}


extern int
tcvp_seek(tcwidget_t *w, void *p)
{
    double pos = *((double*)p);
    uint64_t time = s_length * pos * 1000000;

    tcvp_seek_event_t *se = tcvp_alloc_event(TCVP_SEEK, time, TCVP_SEEK_ABS);
    eventq_send(qs, se);
    tcfree(se);
    return 0;
}


extern int
tcvp_close(tcwidget_t *w, void *p)
{
    quit = 1;
    w->common.skin->enabled = 0;

    tcvp_stop(w, NULL);

    tc2_request(TC2_UNLOAD_ALL, 0);

    XDestroyWindow(xd, w->common.skin->xw);
    XSync(xd, False);

    return 0;
}

extern int
tcvp_add_file(char *file)
{
/*     fprintf(stderr, "%s\n", file); */
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PL_ADD, &file, 1, -1);
    eventq_send(qs, te);
    tcfree(te);

    return 0;
}
