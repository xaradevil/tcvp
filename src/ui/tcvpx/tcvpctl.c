/**
    Copyright (C) 2003, 2004  Michael Ahlberg, Måns Rullgård

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

#include "tcvpx.h"
#include "tcvpctl.h"
#include <string.h>
#include <unistd.h>

static int tcvpstate=-1;
int s_time;
int s_length;
static int show_time = TCTIME_ELAPSED;

extern int
tcvpx_event(tcvp_module_t *tm, tcvp_event_t *te)
{
    muxed_stream_t *st = NULL;

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

    } else if(te->type == TCVP_LOAD || te->type == TCVP_STREAM_INFO) {
	if(te->type == TCVP_LOAD) {
	    char *title;

	    st = ((tcvp_load_event_t *)te)->stream;
	    tcref(st);

	    s_length = 0;

	    if((title = tcattr_get(st, "title"))){
		change_text("title", title);
	    } else {
		char *ext;
		char *file = tcattr_get(st, "file");

		if(file){
		    title = strrchr(file, '/');
		    title = strdup(title? title + 1: file);
		    ext = strrchr(title, '.');
		    if(ext)
			*ext = 0;

		    change_text("title", title);
		    free(title);
		} else {
		    change_text("title", NULL);
		}
	    }

	    change_text("performer", tcattr_get(st, "performer"));
	}

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

			sprintf(buf, "%d", s->audio.channels);
			change_text("audio_channels", buf);
		    }
		}
	    }
	    if(st->time)
		s_length = st->time / 27000000;

	    tcfree(st);
	    st = NULL;

	    update_time();
	}
    }

    return 0;
}


extern int
tcvp_pause(xtk_widget_t *w, void *p)
{
    tcvp_event_send(qs, TCVP_PAUSE);
    return 0;
}


extern int
tcvp_stop(xtk_widget_t *w, void *p)
{
    tcvp_event_send(qs, TCVP_PL_STOP);
    tcvp_event_send(qs, TCVP_CLOSE);

    return 0;
}


extern int
tcvp_play(xtk_widget_t *w, void *p)
{
    if(tcvpstate == TCVP_STATE_STOPPED) {
	tcvp_pause(w, p);
    } else {
	if(tcvpstate != TCVP_STATE_PLAYING) {
	    tcvp_event_send(qs, TCVP_PL_START);
	} else {
	    tcvp_stop(w, p);
	    tcvp_event_send(qs, TCVP_PL_START);
	}
    }

    return 0;
}


extern int
tcvp_next(xtk_widget_t *w, void *p)
{
    tcvp_event_send(qs, TCVP_PL_NEXT);

    return 0;
}


extern int
tcvp_previous(xtk_widget_t *w, void *p)
{
    tcvp_event_send(qs, TCVP_PL_PREV);

    return 0;
}


extern int
tcvp_seek(xtk_widget_t *w, void *p)
{
    double pos = *((double*)p);
    uint64_t time = s_length * pos * 27000000;

    tcvp_event_send(qs, TCVP_SEEK, time, TCVP_SEEK_ABS);

    return 0;
}


extern int
tcvp_seek_rel(xtk_widget_t *w, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    if(wd) {
	char *d = wd->action_data;
	uint64_t time = strtol(d, NULL, 0) * 27000000;

	tcvp_event_send(qs, TCVP_SEEK, time, TCVP_SEEK_REL);
    }

    return 0;
}


extern int
tcvp_quit(void)
{
    tc2_request(TC2_UNLOAD_MODULE, 0, "TCVP/ui/cmdline");

    return 0;
}


extern int
tcvp_add_file(char *file)
{
/*     fprintf(stderr, "%s\n", file); */
    char *s = strrchr(file, '.');
    if(s && !strcasecmp(s, ".m3u")){
	tcvp_event_send(qs, TCVP_PL_ADDLIST, file, -1);	
    } else {
	tcvp_event_send(qs, TCVP_PL_ADD, &file, 1, -1);
    }

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
update_time(void)
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

    int m = t/60;

    snprintf(text, 8, "%c%d:%02d", sign, m, t%60);
    change_text("time", text);

    return 0;
}


extern int
tcvp_playlist_remove(xtk_widget_t *w, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    char *d = wd->action_data;
    char *next;
    int pos = 0;
    int num = 1;

    pos = strtol(d, &next, 10);
    if(next) num = strtol(next, NULL, 10);

    tcvp_event_send(qs, TCVP_PL_REMOVE, pos, num);

    return 0;
}
