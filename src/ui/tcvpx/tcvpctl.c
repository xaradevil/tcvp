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
int s_time;
int s_length;
static int show_time = TCTIME_ELAPSED;

extern void *
tcvp_event(void *p)
{
    muxed_stream_t *st = NULL;
    int quit = 0;

    while(!quit){
	tcvp_event_t *te = eventq_recv(qr);
	switch(te->type){

	case TCVP_STATE:
	    tcvpstate = te->state.state;
	    switch(te->state.state) {
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
	    break;

	case TCVP_TIMER:
	    s_time = te->timer.time / 27000000;
	    if(s_time > s_length)
		s_length = s_time;
	    update_time();
	    break;

	case TCVP_LOAD:
	    if(st)
		tcfree(st);
	    st = te->load.stream;
	    tcref(st);

	    s_length = 0;

	    if(st->title){
		change_text("title", st->title);
	    } else {
		char *title;
		char *ext;

		title = strrchr(st->file, '/');
		title = strdup(title? title + 1: te->load.stream->file);
		ext = strrchr(title, '.');
		if(ext)
		    *ext = 0;

		change_text("title", title);
		free(title);
	    }

	    change_text("performer", st->performer);

	    /* fall through */

	case TCVP_STREAM_INFO:
	    if(!st)
		break;
	    {
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

	    break;

	case -1:
	    quit = 1;
	    break;
	}
	tcfree(te);
    }
    return NULL;
}


extern int
tcvp_pause(xtk_widget_t *w, void *p)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PAUSE);
    eventq_send(qs, te);
    tcfree(te);
    return 0;
}


extern int
tcvp_stop(xtk_widget_t *w, void *p)
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
tcvp_play(xtk_widget_t *w, void *p)
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
tcvp_next(xtk_widget_t *w, void *p)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PL_NEXT);
    eventq_send(qs, te);
    tcfree(te);

    return 0;
}


extern int
tcvp_previous(xtk_widget_t *w, void *p)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PL_PREV);
    eventq_send(qs, te);
    tcfree(te);

    return 0;
}


extern int
tcvp_seek(xtk_widget_t *w, void *p)
{
    double pos = *((double*)p);
    uint64_t time = s_length * pos * 27000000;

    tcvp_seek_event_t *se = tcvp_alloc_event(TCVP_SEEK, time, TCVP_SEEK_ABS);
    eventq_send(qs, se);
    tcfree(se);
    return 0;
}


extern int
tcvp_seek_rel(xtk_widget_t *w, void *p)
{
    char *d = ((widget_data_t *)w->data)->action_data;
    uint64_t time = strtol(d, NULL, 0) * 27000000;

    tcvp_seek_event_t *se = tcvp_alloc_event(TCVP_SEEK, time, TCVP_SEEK_REL);
    eventq_send(qs, se);
    tcfree(se);
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
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PL_ADD, &file, 1, -1);
    eventq_send(qs, te);
    tcfree(te);

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
