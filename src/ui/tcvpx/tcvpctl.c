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
#include "tcvpctl.h"
#include <string.h>
#include <unistd.h>

static int tcvpstate=-1;
int s_time;
int s_length;
static int show_time = TCTIME_ELAPSED;
static int TCVP_STATE, TCVP_TIMER, TCVP_LOAD, TCVP_STREAM_INFO,
    TCVP_PAUSE, TCVP_SEEK, TCVP_CLOSE, TCVP_PL_STOP, TCVP_PL_START,
    TCVP_PL_NEXT, TCVP_PL_PREV, TCVP_PL_ADD;

extern int
init_events(void)
{
    TCVP_STATE = tcvp_get_event("TCVP_STATE");
    TCVP_TIMER = tcvp_get_event("TCVP_TIMER");
    TCVP_LOAD = tcvp_get_event("TCVP_LOAD");
    TCVP_STREAM_INFO = tcvp_get_event("TCVP_STREAM_INFO");
    TCVP_PAUSE = tcvp_get_event("TCVP_PAUSE");
    TCVP_SEEK = tcvp_get_event("TCVP_SEEK"); 
    TCVP_CLOSE = tcvp_get_event("TCVP_CLOSE");
    TCVP_PL_STOP = tcvp_get_event("TCVP_PL_STOP");
    TCVP_PL_START = tcvp_get_event("TCVP_PL_START");
    TCVP_PL_NEXT = tcvp_get_event("TCVP_PL_NEXT");
    TCVP_PL_PREV = tcvp_get_event("TCVP_PL_PREV");
    TCVP_PL_ADD = tcvp_get_event("TCVP_PL_ADD");

    return 0;
}

extern void *
tcvp_event(void *p)
{
    muxed_stream_t *st = NULL;
    int quit = 0;

    while(!quit){
	tcvp_event_t *te = eventq_recv(qr);

	if(te->type == TCVP_STATE) {
	    tcvpstate = ((tcvp_state_event_t *)te)->state;

	    switch(((tcvp_state_event_t *)te)->state) {
	    case TCVP_STATE_PL_END:
		tcvp_stop(NULL, NULL);
		break;

	    case TCVP_STATE_PLAYING:
		change_text("state", "play");
		break;

	    case TCVP_STATE_STOPPED:
		change_text("state", "pause");
		break;

	    case TCVP_STATE_END:
		change_text("state", "stop");
		if(st)
		    tcfree(st);
		st = NULL;
		break;
	    }

	} else if(te->type == TCVP_TIMER) {
	    s_time = ((tcvp_timer_event_t *)te)->time / 27000000;
	    if(s_time > s_length)
		s_length = s_time;
	    update_time();

	} else if(te->type == TCVP_LOAD) {
	    if(st)
		tcfree(st);
	    st = ((tcvp_load_event_t *)te)->stream;
	    tcref(st);

	    s_length = 0;

	    if(st->title){
		change_text("title", st->title);
	    } else {
		char *title;
		char *ext;

		title = strrchr(st->file, '/');
		title = strdup(title? title + 1: st->file);
		ext = strrchr(title, '.');
		if(ext)
		    *ext = 0;

		change_text("title", title);
		free(title);
	    }

	    change_text("performer", st->performer);

	} else if(te->type == TCVP_LOAD || te->type == TCVP_STREAM_INFO) {
	    if(st) {
		int i;
		for(i = 0; i < st->n_streams; i++) {
		    if(st->used_streams[i]) {
			stream_t *s = &st->streams[i];
			if(s->stream_type == STREAM_TYPE_VIDEO) {
			    char buf[10];
			    sprintf(buf, "%.3f",
				    (double)s->video.frame_rate.num /
				    s->video.frame_rate.den);
			    change_text("video_framerate", buf);
			} else if(s->stream_type == STREAM_TYPE_AUDIO) {
			    char buf[10];
			    sprintf(buf, "%d", s->audio.bit_rate/1000);
			    change_text("audio_bitrate", buf);
			    
			    sprintf(buf, "%.1f",
				    (double)s->audio.sample_rate/1000);
			    change_text("audio_samplerate", buf);
			}
		    }
		}
	    }
	    if(st->time)
		s_length = st->time / 27000000;

	    update_time();

	} else if(te->type == -1) {
	    quit = 1;
	}

	tcfree(te);
    }
    return NULL;
}


extern int
tcvp_pause(xtk_widget_t *w, void *p)
{
    tcvp_send_event(qs, TCVP_PAUSE);
    return 0;
}


extern int
tcvp_stop(xtk_widget_t *w, void *p)
{
    tcvp_send_event(qs, TCVP_PL_STOP);
    tcvp_send_event(qs, TCVP_CLOSE);

    return 0;
}


extern int
tcvp_play(xtk_widget_t *w, void *p)
{
    if(tcvpstate == TCVP_STATE_STOPPED) {
	tcvp_pause(w, p);
    } else {
	if(tcvpstate != TCVP_STATE_PLAYING) {
	    tcvp_send_event(qs, TCVP_PL_START);
	} else {
	    tcvp_stop(w, p);
	    tcvp_send_event(qs, TCVP_PL_START);
	}
    }

    return 0;
}


extern int
tcvp_next(xtk_widget_t *w, void *p)
{
    tcvp_send_event(qs, TCVP_PL_NEXT);

    return 0;
}


extern int
tcvp_previous(xtk_widget_t *w, void *p)
{
    tcvp_send_event(qs, TCVP_PL_PREV);

    return 0;
}


extern int
tcvp_seek(xtk_widget_t *w, void *p)
{
    double pos = *((double*)p);
    uint64_t time = s_length * pos * 27000000;

    tcvp_send_event(qs, TCVP_SEEK, time, TCVP_SEEK_ABS);

    return 0;
}


extern int
tcvp_seek_rel(xtk_widget_t *w, void *p)
{
    char *d = ((widget_data_t *)w->data)->action_data;
    uint64_t time = strtol(d, NULL, 0) * 27000000;

    tcvp_send_event(qs, TCVP_SEEK, time, TCVP_SEEK_REL);

    return 0;
}


extern int
tcvp_quit()
{
    tcvp_stop(NULL, NULL);

    tc2_request(TC2_UNLOAD_ALL, 0);

    return 0;
}

extern int
tcvp_add_file(char *file)
{
/*     fprintf(stderr, "%s\n", file); */
    tcvp_send_event(qs, TCVP_PL_ADD, &file, 1, -1);

    return 0;
}

extern int
toggle_time(xtk_widget_t *w, void *p)
{
    if(show_time == TCTIME_ELAPSED) {
	show_time = TCTIME_REMAINING;
    } else if(show_time == TCTIME_REMAINING) {
	show_time = TCTIME_ELAPSED;
    }
    update_time();

    return 0;
}

extern int
update_time()
{
    char text[8];
    int t = 0;
    char sign = ' ';
    double *pos = malloc(sizeof(*pos));

    if(show_time == TCTIME_ELAPSED) {
	t = s_time;
    } else if(show_time == TCTIME_REMAINING) {
	sign = '-';
	if(s_length > 0){
	    t = s_length - s_time;
	} else {
	    t = 0;
	}
    }

    *pos = (s_length>0)?(double)s_time/s_length:-1;
    change_variable("position", pos);

    char *spaces;
    int m = t/60;
    if(m < 10){
	spaces = "  ";
    } else if(m >= 10 && m < 100){
	spaces = " ";
    } else {
	spaces = "";
    }

    snprintf(text, 8, "%s%c%d:%02d", spaces, sign, m, t%60);
    change_text("time", text);

    return 0;
}
