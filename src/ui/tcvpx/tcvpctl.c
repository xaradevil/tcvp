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


extern void *
tcvp_event(void *p)
{
    int r = 1;
    skin_t *skin = p;
    muxed_stream_t *st = NULL;
    char *title;

    while(r){
	tcvp_event_t *te = eventq_recv(qr);
/* 	printf("%d\n", te->type); */
	switch(te->type){
	case TCVP_STATE:
	    switch(te->state.state){
	    case TCVP_STATE_PLAYING:
		p_state = PLAYING;
		break;

	    case TCVP_STATE_STOPPED:
		p_state = PAUSED;
		break;

	    case TCVP_STATE_ERROR:
		printf("Error opening file.\n");
	    case TCVP_STATE_END:
		s_time = 0;
		s_length = 0;
		update_time(skin);
		if(st){
		    tcfree(st);
		    st = NULL;
		}
		if(p_state == PLAYING) {
		    tcvp_next((tcwidget_t *)skin->background, NULL);
		}
	    }
	    break;

	case TCVP_TIMER:
	    s_time = te->timer.time/1000000;
	    update_time(skin);
	    break;

	case TCVP_LOAD:
	    if(st)
		tcfree(st);
	    st = te->load.stream;
	    tcref(st);

/* 	    printf("%s %s %s\n", st->title, st->performer, st->file); */
	    if(st->title){
		if(st->performer){
		    title = alloca(strlen(st->title) + strlen(st->performer) + 4);
		    sprintf(title, "%s - %s", st->performer, st->title);
		} else {
		    title = alloca(strlen(st->title)+1);
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


	    if(skin->title) {
		change_label(skin->title, title);
	    }

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

	case -1:
	    r = 0;
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
    tcvp_event_t *te = tcvp_alloc_event(TCVP_CLOSE);
    p_state = STOPPED;
    eventq_send(qs, te);
    tcfree(te);

    return 0;
}


extern int
tcvp_play(tcwidget_t *w, void *p)
{
    if(p_state == PLAYING) {
	tcvp_stop(NULL, NULL);
    } if(current_file != NULL) {
	tcvp_open_event_t *te = tcvp_alloc_event(TCVP_OPEN);
	te->file = current_file;
	eventq_send(qs, te);
	tcfree(te);
	tcvp_pause(NULL, NULL);
    } else if(p_state == PAUSED) {
	tcvp_pause(NULL, NULL);
    }

    return 0;
}


extern int
tcvp_next(tcwidget_t *w, void *p)
{
    int state_tmp = p_state;
    tcvp_stop(NULL, NULL);

    if((current_file = list_next(files, &flist_curr))!=NULL) {
	if(state_tmp == PLAYING) tcvp_play(NULL, NULL);
    } else { 
	p_state = STOPPED;
	change_label(w->common.skin->title, "Stopped");
    }

    return 0;
}


extern int
tcvp_previous(tcwidget_t *w, void *p)
{
    int state_tmp = p_state;

    tcvp_stop(NULL, NULL);

    if((current_file = list_prev(files, &flist_curr))!=NULL) {
	if(state_tmp == PLAYING) tcvp_play(NULL, NULL);
    } else {
	p_state = STOPPED;
	change_label(w->common.skin->title, "Stopped");
    }

    return 0;
}


extern int
tcvp_seek(tcwidget_t *w, void *p)
{
    double pos = *((double*)p);

    tcvp_seek_event_t *se = tcvp_alloc_event(TCVP_SEEK);
    se->time = s_length * pos * 1000000;
    se->how = TCVP_SEEK_ABS;
    eventq_send(qs, se);
    tcfree(se);
    return 0;
}


extern int
tcvp_close(tcwidget_t *w, void *p)
{
    tcvp_stop(w, NULL);

    tc2_request(TC2_UNLOAD_ALL, 0);

    return 0;
}
